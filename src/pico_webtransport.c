#include "pico_webtransport.h"
#include "picoquic.h"
#include "h3zero_common.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ========== QUIC varint encode helper ========== */
static size_t wt_varint_encode(uint64_t v, uint8_t* buf, size_t cap)
{
    if (v <= 0x3F) {                     /* 1B */
        if (cap < 1) return 0;
        buf[0] = (uint8_t)v;
        return 1;
    } else if (v <= 0x3FFF) {            /* 2B */
        if (cap < 2) return 0;
        buf[0] = 0x40 | (uint8_t)((v >> 8) & 0x3F);
        buf[1] = (uint8_t)(v & 0xFF);
        return 2;
    } else if (v <= 0x3FFFFFFF) {        /* 4B */
        if (cap < 4) return 0;
        buf[0] = 0x80 | (uint8_t)((v >> 24) & 0x3F);
        buf[1] = (uint8_t)((v >> 16) & 0xFF);
        buf[2] = (uint8_t)((v >> 8) & 0xFF);
        buf[3] = (uint8_t)(v & 0xFF);
        return 4;
    } else {                              /* 8B */
        if (cap < 8) return 0;
        buf[0] = 0xC0 | (uint8_t)((v >> 56) & 0x3F);
        buf[1] = (uint8_t)((v >> 48) & 0xFF);
        buf[2] = (uint8_t)((v >> 40) & 0xFF);
        buf[3] = (uint8_t)((v >> 32) & 0xFF);
        buf[4] = (uint8_t)((v >> 24) & 0xFF);
        buf[5] = (uint8_t)((v >> 16) & 0xFF);
        buf[6] = (uint8_t)((v >> 8) & 0xFF);
        buf[7] = (uint8_t)(v & 0xFF);
        return 8;
    }
}

/* ========== SETTINGS/TP 준비 (여기서는 h3zero 기본 설정 사용) ========== */
void picowt_set_transport_parameters(picoquic_cnx_t* cnx)
{
    (void)cnx;
    /* h3zero는 기본 SETTINGS 프레임에 ENABLE_CONNECT_PROTOCOL, H3_DATAGRAM,
       WebTransport 관련 항목을 포함하도록 구성되어 있습니다.
       별도 조정이 필요하면 이 함수에서 확장하세요. RFC 9297의 H3_DATAGRAM=0x33. :contentReference[oaicite:1]{index=1} */
}

/* ========== 컨트롤(CONNECT) 스트림 컨텍스트 준비(주로 클라이언트용) ========== */
h3zero_stream_ctx_t* picowt_set_control_stream(picoquic_cnx_t* cnx, h3zero_callback_ctx_t* h3_ctx)
{
    (void)cnx; /* 실제 스트림 생성은 상위에서 함 */
    h3zero_stream_ctx_t* sc = (h3zero_stream_ctx_t*)malloc(sizeof(*sc));
    if (!sc) return NULL;
    memset(sc, 0, sizeof(*sc));
    sc->path_callback_ctx = h3_ctx;  /* 연결의 h3 콜백 컨텍스트를 보관 */
    sc->stream_id = 0;               /* CONNECT 스트림 ID는 상위에서 설정됨 */
    return sc;
}

/* ========== 로컬 스트림 생성 (WT 헤더 포함 전송) ========== */
h3zero_stream_ctx_t* picowt_create_local_stream(picoquic_cnx_t*        cnx,
                                                int                    is_bidir,
                                                h3zero_callback_ctx_t* h3_ctx,
                                                uint64_t               control_stream_id)
{
    /* 1) 새 로컬 스트림 ID */
    uint64_t sid = picoquic_get_next_local_stream_id(cnx, is_bidir);

    h3zero_stream_ctx_t* sc = (h3zero_stream_ctx_t*)malloc(sizeof(*sc));
    if (!sc) return NULL;
    memset(sc, 0, sizeof(*sc));

    sc->stream_id         = sid;
    sc->path_callback_ctx = h3_ctx;

    /* 2) WT 유니스트림이면 스펙 헤더 붙이기:
          Stream Type = 0x54, 이어서 Session ID(= CONNECT 컨트롤 스트림 ID) varint. */
    if (!is_bidir) {
        uint8_t hdr[16];
        size_t off = 0;

        size_t n = wt_varint_encode(/*0x54*/ 0x54, hdr + off, sizeof(hdr) - off);
        if (n == 0) goto fail;
        off += n;

        n = wt_varint_encode(control_stream_id, hdr + off, sizeof(hdr) - off);
        if (n == 0) goto fail;
        off += n;

        int ret = picoquic_add_to_stream(cnx, sid, hdr, (size_t)off, /*fin*/0);
        if (ret != 0) goto fail;
    }
    else {
        /* BIDI 스트림의 경우에도 스펙상 “특수 시그널 varint”를 서두에 보냅니다.
           여기서는 애플리케이션에서 필요해지면 확장하세요. (최소 구현) */
    }

    return sc;

fail:
    free(sc);
    return NULL;
}

/* ========== 클라이언트 전용: CONNECT (최소 더미) ========== */
int picowt_connect(picoquic_cnx_t* cnx,
                   h3zero_callback_ctx_t* h3_ctx,
                   h3zero_stream_ctx_t* stream_ctx,
                   const char* authority,
                   const char* path,
                   picohttp_post_data_cb_fn wt_callback,
                   void* wt_ctx)
{
    (void)cnx; (void)h3_ctx; (void)stream_ctx;
    (void)authority; (void)path; (void)wt_callback; (void)wt_ctx;
    /* 이 프로젝트에서는 서버 측만 사용하므로 최소 더미. 필요 시 h3zero로
       CONNECT 헤더(QPACK) 생성 및 전송 구현을 추가하세요. */
    return 0;
}

/* ========== 세션 종료/드레인 캡슐(최소 더미) ========== */
int picowt_send_close_session_message(picoquic_cnx_t* cnx,
                                      h3zero_stream_ctx_t* control_stream_ctx,
                                      uint32_t picowt_err,
                                      const char* err_msg)
{
    (void)cnx; (void)control_stream_ctx; (void)picowt_err; (void)err_msg;
    /* 필요 시 Capsule(0x2843) 작성하여 CONNECT 스트림으로 송신. (no-op) */
    return 0;
}

int picowt_send_drain_session_message(picoquic_cnx_t* cnx,
                                      h3zero_stream_ctx_t* control_stream_ctx)
{
    (void)cnx; (void)control_stream_ctx;
    /* 필요 시 Capsule(0x78AE) 작성하여 CONNECT 스트림으로 송신. (no-op) */
    return 0;
}

/* ========== 캡슐 수신/해제 (최소 더미) ========== */
int picowt_receive_capsule(picoquic_cnx_t* cnx,
                           h3zero_stream_ctx_t* stream_ctx,
                           const uint8_t* bytes, const uint8_t* bytes_max,
                           picowt_capsule_t* capsule,
                           h3zero_callback_ctx_t* h3_ctx)
{
    (void)cnx; (void)stream_ctx; (void)bytes; (void)bytes_max; (void)capsule; (void)h3_ctx;
    /* 필요시 Capsule 파싱 구현. (no-op) */
    return 0;
}

void picowt_release_capsule(picowt_capsule_t* capsule)
{
    (void)capsule;
}

/* ========== 세션 등록 해제 (최소 더미) ========== */
void picowt_deregister(picoquic_cnx_t* cnx,
                       h3zero_callback_ctx_t* h3_ctx,
                       h3zero_stream_ctx_t* control_stream_ctx)
{
    (void)cnx; (void)h3_ctx; (void)control_stream_ctx;
}
