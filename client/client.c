#include "picoquic.h"
#include "picoquic_utils.h"
#include "picoquic_packet_loop.h"
#include "h3zero.h"
#include "h3zero_client.h"
#include "pico_webtransport.h"
#include "picotls.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>   // CTRL+C 처리

static uint64_t g_session_id = UINT64_MAX;
static int frame_index = 0;
static volatile int g_running = 1; // 루프 종료 플래그

/* SIGINT 핸들러 */
static void handle_sigint(int sig) {
    (void)sig;
    printf("\n[Client] Caught SIGINT, shutting down...\n");
    g_running = 0;
}

/* ALPN 선택 */
static size_t my_alpn_select_fn(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count) {
    (void)quic;
    const char* h3 = "h3";
    for (size_t i = 0; i < count; i++) {
        if (list[i].len == strlen(h3) &&
            memcmp(list[i].base, h3, list[i].len) == 0) {
            printf("[ALPN] Matched 'h3' at index %zu\n", i);
            return i;
        }
    }
    printf("[ALPN] No matching protocol found.\n");
    return count;
}

/* JPEG 저장 */
static void save_frame(const uint8_t* data, size_t len) {
    char filename[64];
    snprintf(filename, sizeof(filename), "frame_%06d.jpg", frame_index++);
    FILE* f = fopen(filename, "wb");
    if (f) {
        fwrite(data, 1, len, f);
        fclose(f);
        printf("[Client] Saved %s (%zu bytes)\n", filename, len);
    }
}

/* QUIC varint 디코딩 */
static size_t quic_varint_decode(const uint8_t* buf, size_t buf_len, uint64_t* val) {
    if (buf_len < 1) return 0;
    uint8_t first = buf[0];
    size_t len = 1 << (first >> 6);
    if (len > buf_len) return 0;
    *val = first & ((1 << (8 - len * 2)) - 1);
    for (size_t i = 1; i < len; i++) {
        *val = (*val << 8) | buf[i];
    }
    return len;
}

/* QUIC/H3 콜백 */
static int client_callback(picoquic_cnx_t* cnx, uint64_t stream_id, uint8_t* bytes,
                           size_t length, picoquic_call_back_event_t ev,
                           void* callback_ctx, void* stream_ctx) {
    (void)callback_ctx; (void)stream_ctx;

    printf("[Client][Callback] ev=%d stream_id=%llu length=%zu\n",
           ev, (unsigned long long)stream_id, length);

    switch (ev) {
    case picoquic_callback_stream_data:
        if ((stream_id & 0x3) == 0x0) {
            /* CONNECT 응답 */
            printf("[Client] CONNECT response received (stream %llu)\n", (unsigned long long)stream_id);
            g_session_id = stream_id;
        }
        else if ((stream_id & 0x3) == 0x3) {
            /* server → client uni-stream */
            if (length > 2) {
                uint64_t frame_type = 0, sid = 0;
                size_t off = 0;
                off += quic_varint_decode(bytes + off, length - off, &frame_type);
                off += quic_varint_decode(bytes + off, length - off, &sid);

                if (frame_type == 0x54 && sid == g_session_id) {
                    save_frame(bytes + off, length - off);
                }
            }
        }
        break;

    case picoquic_callback_stream_fin:
        printf("[Client] Stream %llu finished.\n", (unsigned long long)stream_id);
        break;

    default:
        break;
    }
    return 0;
}

/* HTTP/3 CONNECT 요청 */
static void send_connect_request(picoquic_cnx_t* cnx, const char* path, const char* server_name) {
    uint64_t stream_id = picoquic_get_next_local_stream_id(cnx, 0); // bidirectional
    h3zero_client_create_connect_request(cnx, stream_id, server_name, path, 1 /* FIN */);
    printf("[Client] Sent HTTP/3 CONNECT to %s on stream %llu\n",
           path, (unsigned long long)stream_id);
}

/* packet_loop 종료 조건 콜백 */
/* packet_loop 종료 조건 콜백 */
static int loop_callback(picoquic_quic_t* quic,
                         picoquic_packet_loop_cb_enum cb_mode,
                         void* callback_ctx,
                         void* callback_arg) {
    (void)quic; (void)cb_mode; (void)callback_ctx; (void)callback_arg;
    return g_running ? 0 : 1; // 0이면 계속, 1이면 종료
}


int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server> <port>\n", argv[0]);
        return -1;
    }
    const char* server_name = argv[1];
    int port = atoi(argv[2]);

    /* CTRL+C 시그널 핸들러 등록 */
    signal(SIGINT, handle_sigint);

    printf("[Client] Starting QUIC client...\n");
    picoquic_quic_t* quic = picoquic_create(8, NULL, NULL, NULL, "h3",
        h3zero_callback, NULL, NULL, NULL, NULL,
        picoquic_current_time(), NULL, NULL, NULL, 0);

    if (!quic) {
        fprintf(stderr, "[Client][Error] picoquic_create failed\n");
        return -1;
    }
    printf("[Client] picoquic_create success\n");

    picoquic_set_alpn_select_fn(quic, my_alpn_select_fn);

    picoquic_cnx_t* cnx = picoquic_create_cnx(quic, picoquic_null_connection_id,
        picoquic_null_connection_id, NULL, picoquic_current_time(),
        0, server_name, "h3", 1);

    if (!cnx) {
        fprintf(stderr, "[Client][Error] Failed to create connection.\n");
        picoquic_free(quic);
        return -1;
    }
    printf("[Client] Connection object created\n");

    picoquic_set_callback(cnx, client_callback, NULL);

    if (picoquic_start_client_cnx(cnx) != 0) {
        fprintf(stderr, "[Client][Error] Failed to start connection.\n");
        picoquic_free(quic);
        return -1;
    }
    printf("[Client] Connection start initiated\n");

    /* HTTP/3 세션 초기화 대기 */
    picoquic_packet_loop(quic, 0, 0, 0, 0, 0, loop_callback, NULL);

    if (!g_running) {
        picoquic_free(quic);
        return 0;
    }

    /* CONNECT 요청 */
    send_connect_request(cnx, "/camera", server_name);

    /* 본 패킷 루프 */
    picoquic_packet_loop(quic, 0, 0, 0, 0, 0, loop_callback, NULL);

    picoquic_free(quic);
    return 0;
}
