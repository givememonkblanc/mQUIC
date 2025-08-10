#include "pico_webtransport.h"
#include "picoquic.h"
#include "h3zero_common.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>     /* ceil() */
#include <unistd.h>   /* usleep() */

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


/* ========== SETTINGS/TP 준비 ========== */
void picowt_set_transport_parameters(picoquic_cnx_t* cnx)
{
    if (!cnx) return;

    picoquic_tp_t tp;
    memset(&tp, 0, sizeof(tp));

    tp.initial_max_data = 64 * 1024 * 1024;            // 전체 연결 윈도우
    tp.initial_max_stream_data_uni = 16 * 1024 * 1024; // uni-stream 윈도우
    tp.initial_max_stream_data_bidi_local = 16 * 1024 * 1024;
    tp.initial_max_stream_data_bidi_remote = 16 * 1024 * 1024;

    picoquic_set_transport_parameters(cnx, &tp);
}

/* ========== 컨트롤 스트림 컨텍스트 준비 ========== */
h3zero_stream_ctx_t* picowt_set_control_stream(picoquic_cnx_t* cnx, h3zero_callback_ctx_t* h3_ctx)
{
    (void)cnx;
    h3zero_stream_ctx_t* sc = (h3zero_stream_ctx_t*)malloc(sizeof(*sc));
    if (!sc) return NULL;
    memset(sc, 0, sizeof(*sc));
    sc->path_callback_ctx = h3_ctx;
    sc->stream_id = 0;
    return sc;
}

/* ========== 로컬 스트림 생성 + pacing 적용 ========== */
h3zero_stream_ctx_t* picowt_create_local_stream(picoquic_cnx_t*        cnx,
                                                int                    is_bidir,
                                                h3zero_callback_ctx_t* h3_ctx,
                                                uint64_t               control_stream_id)
{
    uint64_t sid = picoquic_get_next_local_stream_id(cnx, is_bidir);

    h3zero_stream_ctx_t* sc = (h3zero_stream_ctx_t*)malloc(sizeof(*sc));
    if (!sc) return NULL;
    memset(sc, 0, sizeof(*sc));

    sc->stream_id         = sid;
    sc->path_callback_ctx = h3_ctx;

    if (!is_bidir) {
        uint8_t hdr[16];
        size_t off = 0;

        size_t n = wt_varint_encode(0x54, hdr + off, sizeof(hdr) - off);
        if (n == 0) goto fail;
        off += n;

        n = wt_varint_encode(control_stream_id, hdr + off, sizeof(hdr) - off);
        if (n == 0) goto fail;
        off += n;

        /* WebTransport 유니스트림 헤더 전송 */
        int ret = picoquic_add_to_stream(cnx, sid, hdr, off, 0);
        if (ret != 0) goto fail;
    }

    return sc;

fail:
    free(sc);
    return NULL;
}

/* ========== 클라이언트 전용: CONNECT (더미) ========== */
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
    return 0;
}

/* ========== 세션 종료/드레인 (더미) ========== */
int picowt_send_close_session_message(picoquic_cnx_t* cnx,
                                      h3zero_stream_ctx_t* control_stream_ctx,
                                      uint32_t picowt_err,
                                      const char* err_msg)
{
    (void)cnx; (void)control_stream_ctx; (void)picowt_err; (void)err_msg;
    return 0;
}

int picowt_send_drain_session_message(picoquic_cnx_t* cnx,
                                      h3zero_stream_ctx_t* control_stream_ctx)
{
    (void)cnx; (void)control_stream_ctx;
    return 0;
}

/* ========== 캡슐 수신/해제 (더미) ========== */
int picowt_receive_capsule(picoquic_cnx_t* cnx,
                           h3zero_stream_ctx_t* stream_ctx,
                           const uint8_t* bytes, const uint8_t* bytes_max,
                           picowt_capsule_t* capsule,
                           h3zero_callback_ctx_t* h3_ctx)
{
    (void)cnx; (void)stream_ctx; (void)bytes; (void)bytes_max; (void)capsule; (void)h3_ctx;
    return 0;
}

void picowt_release_capsule(picowt_capsule_t* capsule)
{
    (void)capsule;
}

/* ========== 세션 등록 해제 (더미) ========== */
void picowt_deregister(picoquic_cnx_t* cnx,
                       h3zero_callback_ctx_t* h3_ctx,
                       h3zero_stream_ctx_t* control_stream_ctx)
{
    (void)cnx; (void)h3_ctx; (void)control_stream_ctx;
}
