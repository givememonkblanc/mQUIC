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

    /* 공유 프레임 버퍼 (Producer-Consumer) */
    unsigned char*   frame_buffer;
    size_t           frame_buffer_size;
    size_t           ready_size;           /* 현재 준비된 프레임 크기 */
    int              has_frame;            /* 1=새 프레임 준비됨 */
    pthread_mutex_t  fb_lock;              /* 프레임 버퍼 보호용 락 */

    volatile int     is_sending;
    int              frame_count;
    int              max_frames;

    /* QUIC/H3/WT 식별자 */
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
            return i; /* 'h3' 선택 */
        }
    }
    return count; /* 선택 안 함 */
}

/* ---------------------------- Producer ------------------------------- */
/* FPS 기반 카메라 캡처/인코딩. 최신 프레임만 보관(덮어쓰기) */
static void* camera_thread_func(void* arg)
{
    streamer_context_t* ctx = (streamer_context_t*)arg;
    const int     fps = 30;                          /* 필요시 조정 */
    const useconds_t frame_interval = 1000000 / fps; /* us */

    log_write("CAMERA: start (fps=%d).", fps);

    while (ctx->is_sending) {
        /* 임시 버퍼에 캡처 (버퍼 오버런 방지) */
        unsigned char tmp[1024 * 1024];
        int jpeg_size = camera_capture_jpeg(ctx->cam_handle, tmp, sizeof(tmp));

        if (jpeg_size > 0) {
            if ((size_t)jpeg_size > ctx->frame_buffer_size) {
                log_write("WARN: frame too big (%d > %zu) dropped", jpeg_size, ctx->frame_buffer_size);
            } else {
                pthread_mutex_lock(&ctx->fb_lock);
                memcpy(ctx->frame_buffer, tmp, (size_t)jpeg_size);
                ctx->ready_size = (size_t)jpeg_size;
                ctx->has_frame  = 1; /* 최신 프레임 준비됨 */
                pthread_mutex_unlock(&ctx->fb_lock);
            }
        }
        usleep(frame_interval);
    }

    log_write("CAMERA: exit.");
    return NULL;
}

/* ---------------------------- Consumer ------------------------------- */
/* 지속 유니스트림 + chunk 송출 + keep-alive */
static void* network_thread_func(void* arg)
{
    streamer_context_t* ctx = (streamer_context_t*)arg;
    if (!ctx->cnx) {
        log_write("NET: no connection, exit.");
        return NULL;
    }

    const size_t chunk_size = 64 * 1024; /* 64KB chunk */
    uint64_t sid = picoquic_get_next_local_stream_id(ctx->cnx, /*is_unidir=*/1);
    if ((sid & 0x3) != 0x3) {
        fprintf(stderr, "FATAL: expected server-uni (sid%%4==3), got sid=%llu\n", (unsigned long long)sid);
        ctx->is_sending = 0;
        return NULL;
    }

    /* WT 헤더: 0x54 + session-id */
    uint8_t wt_hdr[16]; size_t off = 0;
    off += quic_varint_encode(0x54, wt_hdr + off);
    off += quic_varint_encode(ctx->control_stream_id, wt_hdr + off);
    picoquic_add_to_stream(ctx->cnx, sid, wt_hdr, off, /*fin=*/0);

    uint64_t last_activity = picoquic_current_time();
    log_write("NET: start (sid=%llu).", (unsigned long long)sid);

    while (ctx->is_sending) {
        if (picoquic_get_cnx_state(ctx->cnx) >= picoquic_state_disconnecting) {
            log_write("NET: connection closing.");
            break;
        }

        int had_frame = 0;

        pthread_mutex_lock(&ctx->fb_lock);
        if (ctx->has_frame && ctx->ready_size > 0) {
            /* 최신 프레임 전송 */
            size_t sent = 0;
            size_t sz   = ctx->ready_size;

            while (sent < sz && ctx->is_sending) {
                size_t to_send = (sz - sent > chunk_size) ? chunk_size : (sz - sent);
                int ret = picoquic_add_to_stream(ctx->cnx, sid,
                                                 ctx->frame_buffer + sent, to_send,
                                                 /*fin=*/0);
                if (ret != 0) {
                    log_write("NET: add_to_stream err=%d", ret);
                    ctx->is_sending = 0;
                    break;
                }
                sent += to_send;
                /* 짧은 pacing으로 burst 완화 */
                usleep(1000); /* 1ms */
            }

            /* 프레임 소비 완료 (최신만 유지) */
            ctx->has_frame  = 0;
            ctx->ready_size = 0;
            had_frame = 1;
            ctx->frame_count++;
        }
        pthread_mutex_unlock(&ctx->fb_lock);

        if (had_frame) {
            last_activity = picoquic_current_time();
        } else {
            /* 새 프레임 없음: keep-alive 보조(데이터면 PING 불필요) */
            uint64_t now = picoquic_current_time();
            if (now - last_activity > 1000000) { /* 1s */
                picoquic_add_to_stream(ctx->cnx, sid, (const uint8_t*)"", 0, 0);
                last_activity = now;
            }
            usleep(1000); /* 루프 속도 */
        }

        if (ctx->frame_count >= ctx->max_frames) break;
    }

    /* 세션 종료(FIN) */
    picoquic_add_to_stream(ctx->cnx, sid, NULL, 0, /*fin=*/1);
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
        log_write("EV: stream %llu closed -> stop", (unsigned long long)stream_id);
        if (ctx->is_sending) {
            ctx->is_sending = 0;
            pthread_join(ctx->camera_thread,  NULL);
            pthread_join(ctx->network_thread, NULL);
        }
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

    switch (event) {
    case picohttp_callback_connect:
        log_write("INFO: /camera CONNECT received (sid=%llu)",
                  (unsigned long long)stream_ctx->stream_id);

        /* WT 세션 식별자 저장 */
        ctx->cnx               = cnx;
        ctx->control_stream_id = stream_ctx->stream_id;     /* == session-id */
        ctx->h3ctx             = stream_ctx->path_callback_ctx;

        /* 연결 단위 keep-alive (250ms) */
        picoquic_enable_keep_alive(cnx, 250000);

        /* 리셋 처리용 콜백 등록 */
        picoquic_set_callback(cnx, camera_wt_callback, app_ctx);

        /* 스레드 시작 */
        if (!ctx->is_sending) {
            ctx->frame_count = 0;
            ctx->is_sending  = 1;
            pthread_create(&ctx->camera_thread,  NULL, camera_thread_func,  ctx);
            pthread_create(&ctx->network_thread, NULL, network_thread_func, ctx);
        }
        break;

    case picohttp_callback_reset:
        log_write("INFO: /camera context reset -> stop streaming");
        if (ctx->is_sending) {
            ctx->is_sending = 0;
            pthread_join(ctx->camera_thread,  NULL);
            pthread_join(ctx->network_thread, NULL);
        }
        break;

    default:
        break;
    }
    return 0;
}

/* ------------------------------ 서버 구동 ---------------------------- */
int run_server(void)
{
    log_init("streamer_log.txt");

    streamer_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* 리소스 초기화 */
    ctx.frame_buffer_size = 1024 * 1024; /* 1MB */
    ctx.frame_buffer      = (unsigned char*)malloc(ctx.frame_buffer_size);
    pthread_mutex_init(&ctx.fb_lock, NULL);
    ctx.ready_size        = 0;
    ctx.has_frame         = 0;

    ctx.cam_handle        = camera_create();
    ctx.max_frames        = 1000000000;

    /* HTTP/3 + WebTransport 경로 등록 */
    picohttp_server_path_item_t path_item_list[] = {
        { "/camera", 7, camera_path_callback, &ctx }
    };
    picohttp_server_parameters_t server_param;
    memset(&server_param, 0, sizeof(server_param));
    server_param.path_table    = path_item_list;
    server_param.path_table_nb = 1;
    /* 필요 시:
       server_param.enable_webtransport = 1;
       server_param.enable_h3_datagram  = 1;
    */

    /* QUIC 컨텍스트 생성 */
    picoquic_quic_t* quic = picoquic_create(
        16,               /* nb_connections */
        "cert.pem",       /* cert */
        "key.pem",        /* key  */
        NULL,             /* CN validation file */
        "h3",             /* ALPN */
        h3zero_callback,  /* HTTP/3 콜백 */
        &server_param,    /* HTTP/3 서버 파라미터(경로 테이블 포함) */
        NULL, NULL, NULL, /* ticket key, cert renew, root key (미사용) */
        picoquic_current_time(),
        NULL, NULL, NULL, /* cc algo, qos, cnx_id_callback */
        0                 /* mtu_max */
    );
    if (!quic) {
        log_write("FATAL: picoquic_create failed");
        camera_destroy(ctx.cam_handle);
        pthread_mutex_destroy(&ctx.fb_lock);
        free(ctx.frame_buffer);
        log_close();
        return -1;
    }

    /* 로깅(QLOG/BINLOG) 설정 */
    picoquic_set_binlog(quic, "binlog");
    picoquic_set_qlog(quic,  "qlogs");
    picoquic_use_unique_log_names(quic, 1);

    /* ALPN 선택기: h3 우선 */
    picoquic_set_alpn_select_fn(quic, my_alpn_select_fn);

    log_write("INFO: WebTransport server running on UDP :4433 ...");
    int ret = picoquic_packet_loop(quic, 4433, 0, 0, 0, 0, NULL, NULL);

    /* 종료 정리 */
    picoquic_free(quic);

    if (ctx.is_sending) {
        ctx.is_sending = 0;
        pthread_join(ctx.camera_thread,  NULL);
        pthread_join(ctx.network_thread, NULL);
    }

    camera_destroy(ctx.cam_handle);
    pthread_mutex_destroy(&ctx.fb_lock);
    free(ctx.frame_buffer);
    log_close();
    return ret;
}
