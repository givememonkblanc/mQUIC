#include "camera.h"
#include "logger.h"
#include "qlog.h"
#include "picoquic_binlog.h"
#include "autoqlog.h"
#include "picoquic.h"
#include "picoquic_utils.h"
#include "picoquic_packet_loop.h"
#include "callback.h" // 새로 만든 헤더를 포함

#include "h3zero.h"
#include "h3zero_common.h"
#include "h3zero_protocol.h"
#include "pico_webtransport.h"

#include "picotls.h" // ptls_iovec_t
#include "path_algo.h" // (향후 멀티패스용 훅. 지금은 사용하지 않음)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <stddef.h>

/* ============================== CONFIG =============================== */
#define TARGET_FPS               30
#define FRAME_INTERVAL_US        (1000000 / TARGET_FPS)
#define MAX_BACKLOG_DELAY_US     200000     /* 200ms: 소비 지연되면 이전 프레임 드롭 */
#define MTU_CHUNK_BYTES          (16 * 1024)/* 스트림 분할 전송 단위 */
#define KEEPALIVE_INTERVAL_US    250000
#define ONE_SECOND_US            1000000
#define FRAME_BUFFER_BYTES       (1024 * 1024)
#define MAX_FRAMES_DEFAULT       1000000000
#define MAX_PATHS 8  /* 동시에 쓸 경로 상한(필요시 늘려도 OK) */

typedef struct {
    int      path_idx;
    uint64_t sid;
    int      ready;
} mp_binding_t;

typedef struct {
    path_algo_t* algo;          /* 알고리즘 상태 */
    uint64_t     sid_legacy;    /* 필요시 유지 (기존 sid 대체용) */
    mp_binding_t bind[MAX_PATHS];
} mp_state_t;

/* ============================= UTILITIES ============================= */
static inline uint64_t now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000ull) + (tv.tv_usec / 1000ull);
}

/* QUIC varint encoder */
static size_t quic_varint_encode(uint64_t v, uint8_t* out)
{
    if (v < (1ull << 6)) {
        out[0] = (uint8_t)v; return 1;
    } else if (v < (1ull << 14)) {
        out[0] = 0x40 | (uint8_t)(v >> 8);
        out[1] = (uint8_t)v; return 2;
    } else if (v < (1ull << 30)) {
        out[0] = 0x80 | (uint8_t)(v >> 24);
        out[1] = (uint8_t)(v >> 16);
        out[2] = (uint8_t)(v >> 8);
        out[3] = (uint8_t)v; return 4;
    } else if (v < (1ull << 62)) {
        out[0] = 0xC0 | (uint8_t)(v >> 56);
        out[1] = (uint8_t)(v >> 48);
        out[2] = (uint8_t)(v >> 40);
        out[3] = (uint8_t)(v >> 32);
        out[4] = (uint8_t)(v >> 24);
        out[5] = (uint8_t)(v >> 16);
        out[6] = (uint8_t)(v >> 8);
        out[7] = (uint8_t)v; return 8;
    }
    return 0;
}

/* ALPN: h3 우선 선택 */
static size_t my_alpn_select_fn(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count)
{
    (void)quic;
    static const char* H3 = "h3";
    for (size_t i = 0; i < count; i++) {
        if (list[i].len == strlen(H3) && memcmp(list[i].base, H3, list[i].len) == 0) {
            return i;
        }
    }
    return count;
}

/* pacing helpers */
static inline void wait_for_pacing(picoquic_cnx_t* cnx, picoquic_path_t* path) {
    uint64_t now_us = picoquic_current_time();
    uint64_t next_us = now_us;
    while (!picoquic_is_sending_authorized_by_pacing(cnx, path, now_us, &next_us)) {
        if (next_us > now_us) {
            usleep((useconds_t)(next_us - now_us));
            now_us = picoquic_current_time();
        }
    }
}
static inline int send_with_pacing(picoquic_cnx_t* cnx, picoquic_path_t* path,
                                   uint64_t sid, const uint8_t* buf,
                                   size_t len, int set_fin)
{
    wait_for_pacing(cnx, path);
    int ret = picoquic_add_to_stream(cnx, sid, buf, len, set_fin);
    if (ret == 0) {
        picoquic_update_pacing_after_send(path, len, picoquic_current_time());
    }
    return ret;
}

/* =========================== APP CONTEXT ============================= */
typedef struct {
    camera_handle_t  cam_handle;

    /* 더블 버퍼 */
    unsigned char*   frame_buffer[2];
    size_t           frame_buffer_size;
    size_t           ready_size[2];
    int              has_frame[2];
    int              active_idx; /* 카메라 쓰기 대상 */
    int              ready_idx;  /* 네트워크 읽기 대상 */

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

    /* 멀티패스(하위 구조로 캡슐화) */
    mp_state_t        mp;
} streamer_context_t;
/* =========================== MP-QUIC FUNC ============================= */
/* 경로 pidx용 전용 uni-stream을 만들고 WT 헤더 전송 + 해당 경로에 affinity */
static int ensure_stream_for_path(streamer_context_t* ctx, int pidx)
{
    if (!ctx || !ctx->cnx) return -1;
    if (pidx < 0 || pidx >= ctx->cnx->nb_paths || pidx >= MAX_PATHS) return -1;

    if (ctx->mp.bind[pidx].ready) return 0;

    picoquic_path_t* path = ctx->cnx->path[pidx];
    if (!path || !path->first_tuple || !path->first_tuple->challenge_verified) {
        return -1; /* 아직 검증 안된 경로 */
    }

    uint64_t sid = picoquic_get_next_local_stream_id(ctx->cnx, 1);
    if ((sid & 0x3) != 0x3) return -1; /* uni-stream 아님 */

    /* WT 헤더: 0x54 + control_stream_id */
    uint8_t hdr[16]; size_t off = 0;
    off += quic_varint_encode(0x54, hdr + off);
    off += quic_varint_encode(ctx->control_stream_id, hdr + off);

    if (send_with_pacing(ctx->cnx, path, sid, hdr, off, 0) != 0) return -1;

    /* 이 스트림은 해당 경로에만 실리도록 고정 */
    picoquic_set_stream_path_affinity(ctx->cnx, sid, path->unique_path_id);

    ctx->mp.bind[pidx].path_idx = pidx;
    ctx->mp.bind[pidx].sid      = sid;
    ctx->mp.bind[pidx].ready    = 1;

    log_write("NET: bound stream %llu to path %d (uid=%llu)",
              (unsigned long long)sid, pidx, (unsigned long long)path->unique_path_id);
    return 0;
}

/* 종료 시 모든 경로-전용 스트림에 FIN */
static void finish_all_bindings(streamer_context_t* ctx)
{
    if (!ctx || !ctx->cnx) return;
    for (int i = 0; i < MAX_PATHS && i < ctx->cnx->nb_paths; i++) {
        if (!ctx->mp.bind[i].ready) continue;
        picoquic_path_t* path = ctx->cnx->path[ ctx->mp.bind[i].path_idx ];
        (void)send_with_pacing(ctx->cnx, path, ctx->mp.bind[i].sid, NULL, 0, 1);
    }
}

/* ========================== FRAME QUEUE API ========================== */
static inline int pop_ready_frame(streamer_context_t* ctx, int* out_idx, size_t* out_sz)
{
    int has = 0;
    *out_idx = -1;
    *out_sz = 0;

    pthread_mutex_lock(&ctx->fb_lock);
    int idx = ctx->ready_idx;
    if (ctx->has_frame[idx] && ctx->ready_size[idx] > 0) {
        has = 1;
        *out_idx = idx;
        *out_sz  = ctx->ready_size[idx];
        ctx->has_frame[idx]  = 0;
        ctx->ready_size[idx] = 0;
    }
    pthread_mutex_unlock(&ctx->fb_lock);
    return has;
}

/* =========================== CAMERA THREAD =========================== */
static void* camera_thread_func(void* arg)
{
    streamer_context_t* ctx = (streamer_context_t*)arg;
    log_write("CAMERA: start (fps=%d).", TARGET_FPS);

    uint64_t backlog_start_us = 0;

    while (ctx->is_sending) {
        /* 네트워크가 아직 못 빼간 프레임이 있으면 백로그 감시 */
        pthread_mutex_lock(&ctx->fb_lock);
        int backlog_full = (ctx->has_frame[ctx->ready_idx] != 0);
        pthread_mutex_unlock(&ctx->fb_lock);

        if (backlog_full) {
            uint64_t now_us = picoquic_current_time();
            if (backlog_start_us == 0) backlog_start_us = now_us;

            if ((now_us - backlog_start_us) > MAX_BACKLOG_DELAY_US) {
                pthread_mutex_lock(&ctx->fb_lock);
                ctx->has_frame[ctx->ready_idx]  = 0; /* 오래된 프레임 폐기 */
                ctx->ready_size[ctx->ready_idx] = 0;
                pthread_mutex_unlock(&ctx->fb_lock);
                backlog_start_us = 0;
            } else {
                usleep(1000);
                continue;
            }
        } else {
            backlog_start_us = 0;
        }

        /* 새 프레임 캡처 → active 버퍼에 쓰기 */
        int idx = ctx->active_idx;
        int jpeg_size = camera_capture_jpeg(ctx->cam_handle,
                                            ctx->frame_buffer[idx],
                                            ctx->frame_buffer_size);

        if (jpeg_size > 0 && (size_t)jpeg_size <= ctx->frame_buffer_size) {
            pthread_mutex_lock(&ctx->fb_lock);
            ctx->ready_size[idx] = (size_t)jpeg_size;
            ctx->has_frame[idx]  = 1;
            /* 버퍼 스왑: active <-> ready */
            int tmp = ctx->active_idx;
            ctx->active_idx = ctx->ready_idx;
            ctx->ready_idx  = tmp;
            pthread_mutex_unlock(&ctx->fb_lock);
        }

        usleep(FRAME_INTERVAL_US);
    }

    log_write("CAMERA: exit.");
    return NULL;
}

/* ========================== NETWORK THREAD =========================== */
static void* network_thread_func(void* arg)
{
    streamer_context_t* ctx = (streamer_context_t*)arg;
    if (!ctx || !ctx->cnx) {
        log_write("NET: no connection, exit.");
        return NULL;
    }

    /* FPS 측정기 */
    uint64_t fps_window_start_ms = 0;
    int fps_count = 0;
    const size_t mtu_size = MTU_CHUNK_BYTES;

    /* 멀티패스 알고리즘 준비 */
    ctx->mp.algo = path_algo_create(ctx->cnx);
    memset(ctx->mp.bind, 0, sizeof(ctx->mp.bind));
    path_algo_refresh(ctx->mp.algo, ctx->cnx);

    /* 최소 1개 경로로 시작 */
    int cur_idx = path_algo_pick_send_path(ctx->mp.algo, ctx->cnx);
    if (cur_idx < 0 || cur_idx >= ctx->cnx->nb_paths) cur_idx = 0;
    (void)ensure_stream_for_path(ctx, cur_idx);
    picoquic_path_t* cur_path = ctx->cnx->path[cur_idx];

    uint64_t last_activity_us = picoquic_current_time();
    log_write("NET: start (paths=%d).", ctx->cnx->nb_paths);

    while (ctx->is_sending) {
        if (picoquic_get_cnx_state(ctx->cnx) >= picoquic_state_disconnecting) break;

        /* === 경로 상태 갱신 & 프로빙 === */
        path_algo_refresh(ctx->mp.algo, ctx->cnx);

        int probe_idx = path_algo_pick_probe_path(ctx->mp.algo, ctx->cnx, picoquic_current_time());
        if (probe_idx >= 0 && probe_idx < ctx->cnx->nb_paths && probe_idx < MAX_PATHS) {
            if (ensure_stream_for_path(ctx, probe_idx) == 0) {
                (void)send_with_pacing(ctx->cnx, ctx->cnx->path[probe_idx],
                                       ctx->mp.bind[probe_idx].sid,
                                       (const uint8_t*)"", 0, 0);
            }
        }

        /* 이번 프레임을 실을 경로 선택 */
        int send_idx = path_algo_pick_send_path(ctx->mp.algo, ctx->cnx);
        if (send_idx < 0 || send_idx >= ctx->cnx->nb_paths) send_idx = cur_idx;
        (void)ensure_stream_for_path(ctx, send_idx);

        /* 프레임 하나 스냅샷 */
        int fb_idx = -1; size_t fb_sz = 0;
        int has_frame = pop_ready_frame(ctx, &fb_idx, &fb_sz);

        if (has_frame && fb_sz > 0) {
            picoquic_path_t* send_path = ctx->cnx->path[send_idx];
            uint64_t sid_for_path = ctx->mp.bind[send_idx].sid;

            /* 1) 길이 varint */
            uint8_t len_buf[8];
            size_t len_len = quic_varint_encode((uint64_t)fb_sz, len_buf);
            if (send_with_pacing(ctx->cnx, send_path, sid_for_path, len_buf, len_len, 0) != 0) {
                log_write("NET: send length varint failed (path=%d)", send_idx);
                ctx->is_sending = 0;
                break;
            }

            /* 2) JPEG payload를 MTU_CHUNK_BYTES로 전송 */
            size_t sent = 0;
            while (sent < fb_sz && ctx->is_sending) {
                size_t to_send = (fb_sz - sent > mtu_size) ? mtu_size : (fb_sz - sent);
                if (send_with_pacing(ctx->cnx, send_path, sid_for_path,
                                     ctx->frame_buffer[fb_idx] + sent,
                                     to_send, 0) != 0) {
                    log_write("NET: payload send failed (path=%d)", send_idx);
                    ctx->is_sending = 0;
                    break;
                }
                sent += to_send;
                path_algo_on_sent(ctx->mp.algo, send_idx, to_send);
            }

            /* FPS */
            ctx->frame_count++;
            fps_count++;
            uint64_t ms_now = now_ms();
            if (fps_window_start_ms == 0) fps_window_start_ms = ms_now;
            if (ms_now - fps_window_start_ms >= 1000) {
                log_write("📡 Actual stream FPS = %d", fps_count);
                fps_count = 0;
                fps_window_start_ms = ms_now;
            }

            last_activity_us = picoquic_current_time();
            cur_idx = send_idx; cur_path = send_path;
        } else {
            /* 유휴 상태: 최근 사용 경로에 1초마다 keep-alive */
            uint64_t now_us = picoquic_current_time();
            if (now_us - last_activity_us > ONE_SECOND_US) {
                if (ctx->mp.bind[cur_idx].ready) {
                    (void)send_with_pacing(ctx->cnx, cur_path, ctx->mp.bind[cur_idx].sid,
                                           (const uint8_t*)"", 0, 0);
                } else {
                    (void)ensure_stream_for_path(ctx, cur_idx);
                }
                last_activity_us = now_us;
                path_algo_on_idle(ctx->mp.algo, now_us);
            }
            usleep(1000);
        }

        if (ctx->frame_count >= ctx->max_frames) break;
    }

    /* 모든 경로 스트림 종료(FIN) */
    finish_all_bindings(ctx);

    if (ctx->mp.algo) { path_algo_destroy(ctx->mp.algo); ctx->mp.algo = NULL; }
    log_write("NET: exit.");
    return NULL;
}
/* ============================== WT CB ================================ */
static int camera_wt_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t ev, void* callback_ctx, void* stream_ctx)
{
    (void)cnx; (void)stream_id; (void)bytes; (void)length; (void)stream_ctx;
    streamer_context_t* ctx = (streamer_context_t*)callback_ctx;

    switch (ev) {
    // <<< 연결이 완전히 닫히는 이벤트를 여기에 추가
    case picoquic_callback_close:
        fprintf(stderr, "[서버-로그] WebTransport 연결 종료됨. 스레드를 정리합니다.\n");
        /* fallthrough */ // 바로 아래 case로 실행을 이어감
    case picoquic_callback_stop_sending:
    case picoquic_callback_stream_reset:
        if (ctx->is_sending) {
            ctx->is_sending = 0;
            // pthread_join은 스레드가 종료될 때까지 기다리는 함수이므로
            // 콜백 함수 안에서 호출하면 데드락을 유발할 수 있습니다.
            // 여기서는 플래그만 설정하고, 스레드 정리는 run_server의 메인 루프 종료 후
            // 또는 별도의 관리 스레드에서 처리하는 것이 더 안전한 설계입니다.
            // 우선은 is_sending 플래그를 통해 스레드들이 스스로 종료되도록 유도합니다.
            fprintf(stderr, "[서버-로그] 전송 중단 플래그 설정 완료.\n");
        }
        break;
    default:
        break;
    }
    return 0;
}


/* ============================ PATH HANDLER =========================== */
static int camera_path_callback(picoquic_cnx_t* cnx,
    uint8_t* bytes, size_t length, picohttp_call_back_event_t event,
    struct st_h3zero_stream_ctx_t* stream_ctx, void* app_ctx)
{
    // 1. 함수 진입 로그
    fprintf(stderr, "==> LOG: camera_path_callback 진입 (이벤트 타입: %d)\n", event);

    (void)bytes; (void)length;
    streamer_context_t* ctx = (streamer_context_t*)app_ctx;

    if (event == picohttp_callback_connect) {
        // 2. CONNECT 이벤트 처리 시작 로그
        fprintf(stderr, "    -> LOG: picohttp_callback_connect 이벤트 처리 시작...\n");

        ctx->cnx               = cnx;
        ctx->control_stream_id = stream_ctx->stream_id;
        ctx->h3ctx             = stream_ctx->path_callback_ctx;
        picoquic_enable_keep_alive(cnx, KEEPALIVE_INTERVAL_US);
        picoquic_set_callback(cnx, camera_wt_callback, app_ctx);

        // ==================== 사용자 코드로 수정 ====================
        // pico_webtransport.c에 직접 구현하신 함수를 호출하여 200 OK 응답을 전송합니다.
        int ret = h3_send_status_200_headers(cnx, stream_ctx->stream_id);
        if (ret != 0) {
            fprintf(stderr, "    <-- FATAL: 200 OK 응답 전송 실패!\n");
            log_write("FATAL: Failed to send 200 OK response.");
            return -1;
        }
        fprintf(stderr, "    --> LOG: 200 OK 응답 전송 성공.\n");
        // ==========================================================

        if (!ctx->is_sending) {
            ctx->frame_count = 0;
            ctx->is_sending  = 1;
            
            // 3. 스레드 생성 직전 로그
            fprintf(stderr, "        --> LOG: 카메라/네트워크 스레드 생성을 시도합니다.\n");

            if (pthread_create(&ctx->camera_thread,  NULL, camera_thread_func,  ctx) != 0 ||
                pthread_create(&ctx->network_thread, NULL, network_thread_func, ctx) != 0) {
                
                // 4. 스레드 생성 실패 로그
                fprintf(stderr, "        <-- FATAL: pthread_create 실패!\n");
                log_write("FATAL: pthread_create failed.");
                ctx->is_sending = 0;
                return -1;
            }
            
            // 5. 스레드 생성 성공 로그
            fprintf(stderr, "        <-- LOG: 스레드 생성 성공.\n");
        }
    }

    // 6. 함수 종료 로그
    fprintf(stderr, "<== LOG: camera_path_callback 정상 종료\n");
    return 0;
}

/* =============================== SERVER ============================== */
int run_server(void)
{
    fprintf(stderr, "[서버-로그] 1. 서버 초기화를 시작합니다...\n");
    log_init("streamer_log.txt");

    streamer_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* 프레임 버퍼 준비 */
    ctx.frame_buffer_size = FRAME_BUFFER_BYTES;
    for (int i = 0; i < 2; i++) {
        ctx.frame_buffer[i] = (unsigned char*)malloc(ctx.frame_buffer_size);
        if (!ctx.frame_buffer[i]) {
            fprintf(stderr, "[서버-오류] 프레임 버퍼[%d] 메모리 할당 실패\n", i);
            for (int j = 0; j < i; j++) free(ctx.frame_buffer[j]);
            return -1;
        }
        ctx.ready_size[i] = 0;
        ctx.has_frame[i]  = 0;
    }
    ctx.active_idx = 0;
    ctx.ready_idx  = 1;
    pthread_mutex_init(&ctx.fb_lock, NULL);

    ctx.cam_handle = camera_create();
    if (!ctx.cam_handle) {
        fprintf(stderr, "[서버-오류] camera_create() 실패\n");
        pthread_mutex_destroy(&ctx.fb_lock);
        for (int i = 0; i < 2; i++) free(ctx.frame_buffer[i]);
        return -1;
    }
    ctx.max_frames = MAX_FRAMES_DEFAULT;
    fprintf(stderr, "[서버-로그] 2. 버퍼 및 카메라 초기화 완료.\n");

    /* H3 path 테이블 */
    picohttp_server_path_item_t path_item_list[] = {
        { "/camera", 7, camera_path_callback, &ctx }
    };
    picohttp_server_parameters_t server_param;
    memset(&server_param, 0, sizeof(server_param));
    server_param.path_table    = path_item_list;
    server_param.path_table_nb = 1;

    /* QUIC 인스턴스 */
    fprintf(stderr, "[서버-로그] 3. QUIC 컨텍스트 생성을 시도합니다...\n");
    picoquic_quic_t* quic = picoquic_create(
        16, "cert.pem", "key.pem", NULL, "h3",
        h3zero_callback, &server_param, NULL, NULL, NULL,
        picoquic_current_time(), NULL, NULL, NULL, 1
    );
    if (!quic) {
        fprintf(stderr, "[서버-오류] picoquic_create() 실패. 현재 폴더에 cert.pem, key.pem 파일이 있는지 확인하세요.\n");
        camera_destroy(ctx.cam_handle);
        pthread_mutex_destroy(&ctx.fb_lock);
        for (int i = 0; i < 2; i++) free(ctx.frame_buffer[i]);
        return -1;
    }
    fprintf(stderr, "[서버-로그] 3. QUIC 컨텍스트 생성 성공.\n");

    /* 🔽🔽 추가: 서버 전역 기본 TP에 멀티패스 켜기 */
    picoquic_tp_t tp; memset(&tp, 0, sizeof(tp));
    picoquic_init_transport_parameters(&tp, 1);
    tp.is_multipath_enabled = 1;
    tp.initial_max_path_id  = 3;
    tp.enable_time_stamp    = 3;
    tp.max_datagram_frame_size = 1280; // <<< 이 한 줄을 추가하세요!
    picoquic_set_default_tp(quic, &tp);
    fprintf(stderr, "[서버-로그] 4. 멀티패스 전송 파라미터 설정 완료.\n");

    /* 로깅 & ALPN */
    picoquic_set_log_level(quic, 1); // <<< 상세 로그 레벨 설정
    picoquic_set_binlog(quic, "binlog");
    picoquic_set_qlog(quic,  "qlogs");
    picoquic_use_unique_log_names(quic, 1);
    picoquic_set_alpn_select_fn(quic, my_alpn_select_fn);
    fprintf(stderr, "[서버-로그] 5. 로깅 및 콜백 설정 완료.\n");

    /* 패킷 루프 */
    fprintf(stderr, "[서버-로그] 6. UDP 포트 4433에서 패킷 루프를 시작합니다. 클라이언트 접속 대기 중...\n");
    picoquic_packet_loop_param_t param = { 0 };
    param.local_port = 4433; 
    int ret = picoquic_packet_loop_v2(quic, &param, NULL, NULL);
    fprintf(stderr, "[서버-로그] 7. 패킷 루프가 종료되었습니다 (반환 코드: %d).\n", ret);

    /* 정리 */
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
    fprintf(stderr, "[서버-로그] 8. 모든 리소스를 정리하고 종료합니다.\n");
    return ret;
}