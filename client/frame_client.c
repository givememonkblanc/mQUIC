/******************************************************************/
/* frame_client.c — WebTransport 클라이언트 샘플 (완결판)         */
/******************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/socket.h>     // AF_UNSPEC
#include <netinet/in.h>     // AF_INET, AF_INET6

#include "picoquic.h"
#include "picoquic_utils.h"
#include "picoquic_packet_loop.h"   // picoquic_packet_loop_v2()
#include "h3zero.h"
#include "h3zero_client.h"
#include "pico_webtransport.h"
#include "picotls.h"

// --- 전역 변수 및 상태 관리 ---
static volatile int running = 1;
static int frame_index = 0;

// --- WT 스트림별 버퍼 컨텍스트 ---
typedef struct st_wt_stream_ctx_t {
    uint64_t stream_id;
    uint8_t* buffer;
    size_t   bufcap;
    size_t   buflen;
    struct st_wt_stream_ctx_t* next;
} wt_stream_ctx_t;

// --- 애플리케이션 전체 컨텍스트 ---
typedef struct {
    uint64_t session_id;           // CONNECT 스트림 ID
    wt_stream_ctx_t* wt_streams;   // 연결된 WT 유니스트림 리스트
} app_ctx_t;

// --- 전방 선언 ---
static void handle_sigint(int sig);
static size_t my_alpn_select_fn(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count);
static int loop_cb(picoquic_quic_t* quic,
                   picoquic_packet_loop_cb_enum mode,
                   void* callback_ctx, void* callback_arg);
static int client_app_callback(picoquic_cnx_t* cnx,
                               uint8_t* bytes, size_t length,
                               picohttp_call_back_event_t event,
                               h3zero_stream_ctx_t* stream_ctx,
                               void* path_ctx);
static wt_stream_ctx_t* get_or_create_wt_stream(app_ctx_t* ctx, uint64_t sid);
static void free_all_wt_streams(app_ctx_t* ctx);
static void save_frame(uint8_t* buf, size_t len);

// --- SIGINT 핸들러 ---
static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
    fprintf(stderr, "\nSIGINT caught, shutting down...\n");
}

// --- ALPN 선택 (h3 우선) ---
static size_t my_alpn_select_fn(picoquic_quic_t* quic,
                                ptls_iovec_t* list, size_t count) {
    (void)quic;
    const char* want = "h3";
    for (size_t i = 0; i < count; i++) {
        if (list[i].len == strlen(want) &&
            memcmp(list[i].base, want, list[i].len) == 0) {
            return i;
        }
    }
    return count;
}

// --- packet_loop_v2 콜백 ---
static int loop_cb(picoquic_quic_t* quic,
                   picoquic_packet_loop_cb_enum mode,
                   void* callback_ctx, void* callback_arg)
{
    (void)quic; (void)mode; (void)callback_ctx; (void)callback_arg;
    if (!running) {
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }
    return 0;
}

// --- WT 유니스트림으로부터 프레임 축적 및 저장 ---
static int client_app_callback(picoquic_cnx_t* cnx,
                               uint8_t* bytes, size_t length,
                               picohttp_call_back_event_t event,
                               h3zero_stream_ctx_t* stream_ctx,
                               void* path_ctx)
{
    app_ctx_t* app = (app_ctx_t*)path_ctx;

    if (event == picohttp_callback_connect) {
        // CONNECT 성공: session_id 저장
        app->session_id = stream_ctx->stream_id;
        printf("[Client] WebTransport session established (id=%" PRIu64 ")\n",
               app->session_id);
    }
    else if (event == picoquic_callback_stream_data
          || event == picoquic_callback_stream_fin) {
        // 서버가 연 WT 유니스트림 데이터
        wt_stream_ctx_t* w = get_or_create_wt_stream(app, stream_ctx->stream_id);
        if (length > 0 && w) {
            if (w->buflen + length > w->bufcap) {
                fprintf(stderr, "Buffer overflow on stream %" PRIu64 "\n", w->stream_id);
            } else {
                memcpy(w->buffer + w->buflen, bytes, length);
                w->buflen += length;
            }
        }
        if (event == picoquic_callback_stream_fin && w && w->buflen > 0) {
            save_frame(w->buffer, w->buflen);
            w->buflen = 0;
        }
    }

    return 0;
}

// --- JPEG 프레임 파일로 저장 ---
static void save_frame(uint8_t* buf, size_t len) {
    char fn[64];
    snprintf(fn, sizeof(fn), "frame_%05d.jpg", frame_index++);
    FILE* f = fopen(fn, "wb");
    if (f) {
        fwrite(buf, 1, len, f);
        fclose(f);
        printf("[Client] Saved %s (%zu bytes)\n", fn, len);
    }
}

// --- WT 유니스트림 컨텍스트 조회/생성 ---
static wt_stream_ctx_t* get_or_create_wt_stream(app_ctx_t* ctx, uint64_t sid) {
    for (wt_stream_ctx_t* p = ctx->wt_streams; p; p = p->next) {
        if (p->stream_id == sid) return p;
    }
    wt_stream_ctx_t* p = malloc(sizeof(*p));
    if (!p) return NULL;
    p->stream_id = sid;
    p->bufcap    = 1024*1024;
    p->buffer    = malloc(p->bufcap);
    p->buflen    = 0;
    p->next      = ctx->wt_streams;
    ctx->wt_streams = p;
    return p;
}

// --- 모든 WT 컨텍스트 해제 ---
static void free_all_wt_streams(app_ctx_t* ctx) {
    wt_stream_ctx_t* p = ctx->wt_streams;
    while(p) {
        wt_stream_ctx_t* nx = p->next;
        free(p->buffer);
        free(p);
        p = nx;
    }
    ctx->wt_streams = NULL;
}

// --- main ---
int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server> <port>\n", argv[0]);
        return 1;
    }
    signal(SIGINT, handle_sigint);

    const char* server_name = argv[1];
    int          server_port = atoi(argv[2]);

    // 1) 앱 컨텍스트 생성
    app_ctx_t* app = calloc(1, sizeof(*app));
    if (!app) {
        perror("calloc");
        return 1;
    }

    // 2) QUIC 컨텍스트 생성
    picoquic_quic_t* quic = picoquic_create(
        8,
        NULL, NULL, NULL,
        "h3",
        NULL, NULL, NULL, NULL, NULL,
        picoquic_current_time(),
        NULL, NULL, NULL,
        0);
    if (!quic) {
        fprintf(stderr, "picoquic_create() failed\n");
        free(app);
        return 1;
    }
    picoquic_set_alpn_select_fn(quic, my_alpn_select_fn);

    // 3) 서버 주소 해석
    struct sockaddr_storage addr;
    int is_name = 0;
    if (picoquic_get_server_address(server_name, server_port,
                                    &addr, &is_name) != 0) {
        fprintf(stderr, "Cannot resolve %s:%d\n", server_name, server_port);
        picoquic_free(quic);
        free(app);
        return 1;
    }

    // 4) 클라이언트 연결 생성
    picoquic_cnx_t* cnx = picoquic_create_cnx(quic,
        picoquic_null_connection_id, picoquic_null_connection_id,
        (struct sockaddr*)&addr, picoquic_current_time(),
        0, server_name, "h3", 1);
    if (!cnx) {
        fprintf(stderr, "picoquic_create_cnx() failed\n");
        picoquic_free(quic);
        free(app);
        return 1;
    }

    // 5) H3/WebTransport 콜백 컨텍스트
    picohttp_server_path_item_t paths[] = {
        { "/camera", 7, client_app_callback, app }
    };
    picohttp_server_parameters_t srvp = {
        .path_table = paths,
        .path_table_nb = 1
    };
    h3zero_callback_ctx_t* h3ctx =
        h3zero_callback_create_context(&srvp);
    if (!h3ctx) {
        fprintf(stderr, "h3zero_callback_create_context() failed\n");
        picoquic_free(quic);
        free(app);
        return 1;
    }

    picoquic_set_callback(cnx, h3zero_callback, h3ctx);

    // 6) 연결 시작
    if (picoquic_start_client_cnx(cnx) != 0) {
        fprintf(stderr, "picoquic_start_client_cnx() failed\n");
        h3zero_callback_delete_context(cnx, h3ctx);
        picoquic_free(quic);
        free(app);
        return 1;
    }

    // 7) packet_loop_v2 파라미터
    picoquic_packet_loop_param_t param;
    memset(&param, 0, sizeof(param));
    param.local_port = 0;         // 랜덤
    param.local_af   = AF_UNSPEC; // IPv4+IPv6

    // 8) 1단계: HTTP/3 핸드셰이크
    if (picoquic_packet_loop_v2(quic, &param, loop_cb, NULL) != 0) {
        fprintf(stderr, "Handshake failed\n");
        goto cleanup;
    }

    // 9) CONNECT:protocol=webtransport
    {
        uint64_t ctrl = picoquic_get_next_local_stream_id(cnx, 0);
        if (h3zero_client_create_connect_request(
                cnx, ctrl, server_name, "/camera", 1) != 0) {
            fprintf(stderr, "CONNECT request failed\n");
            goto cleanup;
        }
        fprintf(stdout, "[Client] CONNECT sent (stream=%" PRIu64 ")\n", ctrl);
    }

    // 10) 2단계: WebTransport 스트림 수신 루프
    picoquic_packet_loop_v2(quic, &param, loop_cb, NULL);

cleanup:
    fprintf(stdout, "[Client] cleaning up...\n");
    free_all_wt_streams(app);
    h3zero_callback_delete_context(cnx, h3ctx);
    picoquic_free(quic);
    free(app);
    return 0;
}
