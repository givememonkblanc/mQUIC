// client/wt_client.h
#pragma once

#include <signal.h>
#include <stdint.h>
#include <stddef.h>

#include "picoquic.h"
#include "picoquic_utils.h"
#include "picoquic_packet_loop.h"
#include "h3zero.h"
#include "h3zero_client.h"
#include "pico_webtransport.h"
#include "picotls.h"

/* ==== 공용 상수 (둘 다 여기서 정의) ==== */
#define CTRL_START        0x01
#define CTRL_STOP         0x02
#define CTRL_SET_FPS      0x10
#define CTRL_SET_BITRATE  0x11

#define DEFAULT_FPS       30
#define DEFAULT_BITRATE   8000000 /* 8 Mbps */

/* ==== 외부 전역 ==== */
extern volatile sig_atomic_t g_running;

/* ==== CLI 옵션 ==== */
typedef struct {
    const char* host;
    const char* addr;      // ★ 실제 접속 주소(옵션) - 없으면 host 사용
    int         port;
    const char* path;
    const char* sni;    // SNI 강제(옵션, 없으면 host 사용)
    const char* alpn;
    const char* out_dir;
    int         max_frames;
    int         no_verify;
} cli_opts_t;

/* ==== 앱 컨텍스트 ==== */
typedef struct wt_stream_t wt_stream_t; /* opaque */
typedef struct app_ctx {
    char              out_dir[512];
    int               max_frames;
    int               saved;
    uint64_t          session_ctrl_id;
    wt_stream_t*      streams;
} app_ctx_t;

/* ==== 공용 API ==== */
int ensure_dir(const char* path);
size_t quic_varint_encode(uint64_t v, uint8_t* out);
size_t quic_varint_decode(const uint8_t* p, size_t len, uint64_t* out);

size_t alpn_select(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count);
int loop_hook(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum mode, void* ctx, void* arg);

/* 제어 유니스트림 송신 */
int send_wt_control(picoquic_cnx_t* cnx, uint64_t session_id,
                    uint8_t code, const uint8_t* payload, size_t payload_len);

/* 서버가 보낸 데이터 처리(스트림 내부 상태 포함) */
int wt_handle_stream_data(app_ctx_t* app, uint64_t sid, uint8_t* bytes, size_t length);

/* 콜백 (H3 핸들러) */
int client_cb(picoquic_cnx_t* cnx, uint8_t* bytes, size_t length,
              picohttp_call_back_event_t event, h3zero_stream_ctx_t* h3s, void* app_v);

void free_streams(app_ctx_t* app);

/* 로그 매크로 (없으면 no-op) */
#ifndef LOGV
#define LOGV(...) ((void)0)
#endif

/* trust-all */
extern ptls_verify_certificate_t PTLS_TRUST_ALL;
