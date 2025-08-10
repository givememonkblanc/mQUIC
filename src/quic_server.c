#include "camera.h"
#include "logger.h"
#include "qlog.h"
#include "picoquic_binlog.h"
#include "autoqlog.h"
#include "picoquic.h"
#include "picoquic_utils.h"
#include "picoquic_packet_loop.h"

#include "h3zero.h"
#include "h3zero_common.h"
#include "pico_webtransport.h"

#include "picotls.h" // ptls_iovec_t

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <stddef.h>

/* ---------------------------- QUIC varint ---------------------------- */
static size_t quic_varint_encode(uint64_t v, uint8_t* out)
{
    if (v < (1ull << 6)) { out[0] = (uint8_t)v; return 1; }
    else if (v < (1ull << 14)) { out[0] = 0x40 | (uint8_t)(v >> 8); out[1] = (uint8_t)v; return 2; }
    else if (v < (1ull << 30)) { out[0] = 0x80 | (uint8_t)(v >> 24); out[1] = (uint8_t)(v >> 16); out[2] = (uint8_t)(v >> 8); out[3] = (uint8_t)v; return 4; }
    else if (v < (1ull << 62)) { out[0] = 0xC0 | (uint8_t)(v >> 56); out[1] = (uint8_t)(v >> 48); out[2] = (uint8_t)(v >> 40); out[3] = (uint8_t)(v >> 32); out[4] = (uint8_t)(v >> 24); out[5] = (uint8_t)(v >> 16); out[6] = (uint8_t)(v >> 8); out[7] = (uint8_t)v; return 8; }
    return 0;
}

/* --------------------------- 앱 컨텍스트 ----------------------------- */
typedef struct {
    camera_handle_t  cam_handle;

    /* 더블 버퍼 */
    unsigned char*   frame_buffer[2];
    size_t           frame_buffer_size;
    size_t           ready_size[2];
    int              has_frame[2];

    int              active_idx; /* 카메라가 쓰는 버퍼 */
    int              ready_idx;  /* 네트워크가 읽는 버퍼 */

    pthread_mutex_t  fb_lock;    /* 버퍼 동기화 */

    volatile int     is_sending;
    int              frame_count;
    int              max_frames;

    /* QUIC/H3/WT */
    picoquic_cnx_t*          cnx;
    h3zero_callback_ctx_t*   h3ctx;
    uint64_t                 control_stream_id;

    /* 스레드 */
    pthread_t         camera_thread;
    pthread_t         network_thread;
} streamer_context_t;

/* --------------------------- 유틸리티/ALPN --------------------------- */
static uint64_t now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000ull) + (tv.tv_usec / 1000ull);
}

static size_t my_alpn_select_fn(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count)
{
    (void)quic;
    const char* h3 = "h3";
    for (size_t i = 0; i < count; i++) {
        if (list[i].len == strlen(h3) && memcmp(list[i].base, h3, list[i].len) == 0) {
            return i;
        }
    }
    return count;
}

#define MAX_BACKLOG_DELAY 200000 // 200ms

static void* camera_thread_func(void* arg)
{
    streamer_context_t* ctx = (streamer_context_t*)arg;
    const int target_fps = 30;
    const useconds_t frame_interval = 1000000 / target_fps;

    log_write("CAMERA: start (fps=%d).", target_fps);

    while (ctx->is_sending) {
        int idx = ctx->active_idx;

        /* 백로그 검사 */
        pthread_mutex_lock(&ctx->fb_lock);
        int backlog_full = (ctx->has_frame[ctx->ready_idx] != 0);
        pthread_mutex_unlock(&ctx->fb_lock);

        if (backlog_full) {
            static uint64_t backlog_start = 0;
            uint64_t now = picoquic_current_time();
            if (backlog_start == 0) backlog_start = now;

            /* 200ms 넘게 backlog 유지되면 이전 프레임 드롭 */
            if ((now - backlog_start) > MAX_BACKLOG_DELAY) {
                pthread_mutex_lock(&ctx->fb_lock);
                ctx->has_frame[ctx->ready_idx] = 0; // 오래된 프레임 버림
                pthread_mutex_unlock(&ctx->fb_lock);
                backlog_start = 0;
            } else {
                usleep(1000);
                continue;
            }
        }

        /* 새 프레임 캡처 */
        int jpeg_size = camera_capture_jpeg(ctx->cam_handle,
                                            ctx->frame_buffer[idx],
                                            ctx->frame_buffer_size);

        if (jpeg_size > 0 && (size_t)jpeg_size <= ctx->frame_buffer_size) {
            pthread_mutex_lock(&ctx->fb_lock);
            ctx->ready_size[idx] = (size_t)jpeg_size;
            ctx->has_frame[idx]  = 1;

            /* 버퍼 스왑 */
            int tmp = ctx->active_idx;
            ctx->active_idx = ctx->ready_idx;
            ctx->ready_idx  = tmp;
            pthread_mutex_unlock(&ctx->fb_lock);
        }

        usleep(frame_interval);
    }

    log_write("CAMERA: exit.");
    return NULL;
}

/* ---------------------------- Consumer ------------------------------- */
static void* network_thread_func(void* arg)
{
    streamer_context_t* ctx = (streamer_context_t*)arg;
    if (!ctx->cnx) {
        log_write("NET: no connection, exit.");
        return NULL;
    }
    static uint64_t fps_start_ms = 0;
    static int fps_count = 0;
    picoquic_path_t* path = ctx->cnx->path[0];
    const size_t mtu_size = 16 * 1024; // 🔹 MTU 확장 (기본 1400 → 16KB)
    uint64_t sid = picoquic_get_next_local_stream_id(ctx->cnx, 1);

    if ((sid & 0x3) != 0x3) {
        fprintf(stderr, "FATAL: wrong sid=%llu\n", (unsigned long long)sid);
        ctx->is_sending = 0;
        return NULL;
    }

    /* WebTransport 헤더 전송 */
    uint8_t wt_hdr[16]; size_t off = 0;
    off += quic_varint_encode(0x54, wt_hdr + off);
    off += quic_varint_encode(ctx->control_stream_id, wt_hdr + off);

    uint64_t now = picoquic_current_time();
    uint64_t next_time = now;
    while (!picoquic_is_sending_authorized_by_pacing(ctx->cnx, path, now, &next_time)) {
        if (next_time > now) {
            usleep((useconds_t)(next_time - now));
            now = picoquic_current_time();
        }
    }
    picoquic_add_to_stream(ctx->cnx, sid, wt_hdr, off, 0);
    picoquic_update_pacing_after_send(path, off, now);

    uint64_t last_activity = picoquic_current_time();
    log_write("NET: start (sid=%llu).", (unsigned long long)sid);

    while (ctx->is_sending) {
        if (picoquic_get_cnx_state(ctx->cnx) >= picoquic_state_disconnecting) break;

        pthread_mutex_lock(&ctx->fb_lock);
        int idx = ctx->ready_idx;
        int has_frame = ctx->has_frame[idx];
        size_t sz = ctx->ready_size[idx];
        if (has_frame && sz > 0) {
            ctx->has_frame[idx] = 0;
            ctx->ready_size[idx] = 0;
        }
        pthread_mutex_unlock(&ctx->fb_lock);

        if (has_frame && sz > 0) {
            size_t sent = 0;

            /* 1️⃣ 프레임 길이 varint로 전송 (클라이언트 파서 호환) */
            uint8_t len_buf[8];
            size_t len_len = quic_varint_encode((uint64_t)sz, len_buf);

            now = picoquic_current_time();
            next_time = now;
            while (!picoquic_is_sending_authorized_by_pacing(ctx->cnx, path, now, &next_time)) {
                if (next_time > now) {
                    usleep((useconds_t)(next_time - now));
                    now = picoquic_current_time();
                }
            }
            picoquic_add_to_stream(ctx->cnx, sid, len_buf, len_len, 0);
            picoquic_update_pacing_after_send(path, len_len, now);

            /* 2️⃣ JPEG 데이터 MTU 단위 전송 */
            while (sent < sz && ctx->is_sending) {
                size_t to_send = (sz - sent > mtu_size) ? mtu_size : (sz - sent);

                now = picoquic_current_time();
                next_time = now;
                while (!picoquic_is_sending_authorized_by_pacing(ctx->cnx, path, now, &next_time)) {
                    if (next_time > now) {
                        usleep((useconds_t)(next_time - now));
                        now = picoquic_current_time();
                    }
                }

                int ret = picoquic_add_to_stream(ctx->cnx, sid,
                                                 ctx->frame_buffer[idx] + sent,
                                                 to_send, 0);
                if (ret != 0) {
                    log_write("NET: add_to_stream err=%d", ret);
                    ctx->is_sending = 0;
                    break;
                }

                picoquic_update_pacing_after_send(path, to_send, now);
                sent += to_send;
            }
            ctx->frame_count++;
            fps_count++;
            uint64_t now = now_ms();
            if (fps_start_ms == 0) fps_start_ms = now;

            if (now - fps_start_ms >= 1000) {
                log_write("📡 Actual stream FPS = %d", fps_count);
                fps_count = 0;
                fps_start_ms = now;
            }
            last_activity = picoquic_current_time();
        } else {
            now = picoquic_current_time();
            if (now - last_activity > 1000000) {
                next_time = now;
                while (!picoquic_is_sending_authorized_by_pacing(ctx->cnx, path, now, &next_time)) {
                    if (next_time > now) {
                        usleep((useconds_t)(next_time - now));
                        now = picoquic_current_time();
                    }
                }
                picoquic_add_to_stream(ctx->cnx, sid, (const uint8_t*)"", 0, 0);
                picoquic_update_pacing_after_send(path, 0, now);
                last_activity = now;
            }
            usleep(1000);
        }

        if (ctx->frame_count >= ctx->max_frames) break;
    }

    picoquic_add_to_stream(ctx->cnx, sid, NULL, 0, 1); // 전체 세션 종료
    log_write("NET: exit.");
    return NULL;
}



/* ----------------------------- WT 콜백 ------------------------------ */
static int camera_wt_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t ev, void* callback_ctx, void* stream_ctx)
{
    (void)cnx; (void)bytes; (void)length; (void)stream_ctx;
    streamer_context_t* ctx = (streamer_context_t*)callback_ctx;

    switch (ev) {
    case picoquic_callback_stop_sending:
    case picoquic_callback_stream_reset:
        ctx->is_sending = 0;
        pthread_join(ctx->camera_thread,  NULL);
        pthread_join(ctx->network_thread, NULL);
        break;
    default:
        break;
    }
    return 0;
}

/* -------------------------- /camera PATH 콜백 ------------------------ */
static int camera_path_callback(picoquic_cnx_t* cnx,
    uint8_t* bytes, size_t length, picohttp_call_back_event_t event,
    struct st_h3zero_stream_ctx_t* stream_ctx, void* app_ctx)
{
    (void)bytes; (void)length;
    streamer_context_t* ctx = (streamer_context_t*)app_ctx;

    if (event == picohttp_callback_connect) {
        ctx->cnx               = cnx;
        ctx->control_stream_id = stream_ctx->stream_id;
        ctx->h3ctx             = stream_ctx->path_callback_ctx;
        picoquic_enable_keep_alive(cnx, 250000);
        picoquic_set_callback(cnx, camera_wt_callback, app_ctx);

        if (!ctx->is_sending) {
            ctx->frame_count = 0;
            ctx->is_sending  = 1;
            pthread_create(&ctx->camera_thread,  NULL, camera_thread_func,  ctx);
            pthread_create(&ctx->network_thread, NULL, network_thread_func, ctx);
        }
    }
    return 0;
}

/* ------------------------------ 서버 구동 ---------------------------- */
int run_server(void)
{
    log_init("streamer_log.txt");
    streamer_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    ctx.frame_buffer_size = 1024 * 1024;
    for (int i = 0; i < 2; i++) {
        ctx.frame_buffer[i] = (unsigned char*)malloc(ctx.frame_buffer_size);
        ctx.ready_size[i]   = 0;
        ctx.has_frame[i]    = 0;
    }
    ctx.active_idx = 0;
    ctx.ready_idx  = 1;
    pthread_mutex_init(&ctx.fb_lock, NULL);

    ctx.cam_handle = camera_create();
    ctx.max_frames = 1000000000;

    picohttp_server_path_item_t path_item_list[] = {
        { "/camera", 7, camera_path_callback, &ctx }
    };
    picohttp_server_parameters_t server_param;
    memset(&server_param, 0, sizeof(server_param));
    server_param.path_table    = path_item_list;
    server_param.path_table_nb = 1;

    picoquic_quic_t* quic = picoquic_create(
        16, "cert.pem", "key.pem", NULL, "h3",
        h3zero_callback, &server_param, NULL, NULL, NULL,
        picoquic_current_time(), NULL, NULL, NULL, 0
    );
    if (!quic) return -1;

    picoquic_set_binlog(quic, "binlog");
    picoquic_set_qlog(quic,  "qlogs");
    picoquic_use_unique_log_names(quic, 1);
    picoquic_set_alpn_select_fn(quic, my_alpn_select_fn);

    int ret = picoquic_packet_loop(quic, 4433, 0, 0, 0, 0, NULL, NULL);

    picoquic_free(quic);
    if (ctx.is_sending) {
        ctx.is_sending = 0;
        pthread_join(ctx.camera_thread,  NULL);
        pthread_join(ctx.network_thread, NULL);
    }
    camera_destroy(ctx.cam_handle);
    pthread_mutex_destroy(&ctx.fb_lock);
    for (int i = 0; i < 2; i++) free(ctx.frame_buffer[i]);
    log_close();
    return ret;
}
