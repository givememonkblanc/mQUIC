#include "pico_webtransport.h"
#include "picoquic.h"
#include "picoquic_utils.h"   /* PRIu64 등 */
#include "h3zero_common.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/* ===== varint helpers ===== */
static size_t wt_varint_encode(uint64_t v, uint8_t* buf, size_t cap)
{
    if (v <= 0x3F) { if (cap<1) return 0; buf[0]=(uint8_t)v; return 1; }
    if (v <= 0x3FFF) { if (cap<2) return 0; buf[0]=0x40|((v>>8)&0x3F); buf[1]=(uint8_t)v; return 2; }
    if (v <= 0x3FFFFFFF) {
        if (cap<4) return 0;
        buf[0]=0x80|((v>>24)&0x3F); buf[1]=(uint8_t)(v>>16); buf[2]=(uint8_t)(v>>8); buf[3]=(uint8_t)v; return 4;
    }
    if (cap<8) return 0;
    buf[0]=0xC0|((v>>56)&0x3F); buf[1]=(uint8_t)(v>>48); buf[2]=(uint8_t)(v>>40); buf[3]=(uint8_t)(v>>32);
    buf[4]=(uint8_t)(v>>24); buf[5]=(uint8_t)(v>>16); buf[6]=(uint8_t)(v>>8); buf[7]=(uint8_t)v; return 8;
}
static size_t wt_varint_decode(const uint8_t* p, const uint8_t* max, uint64_t* v)
{
    if (!p || p>=max) return 0;
    uint8_t b0=*p, pref=b0>>6; size_t len=(pref==0)?1:(pref==1)?2:(pref==2)?4:8;
    if ((size_t)(max-p)<len) return 0;
    uint64_t val=0;
    switch(len){
        case 1: val = b0 & 0x3F; break;
        case 2: val = ((uint64_t)(b0&0x3F)<<8) | p[1]; break;
        case 4: val = ((uint64_t)(b0&0x3F)<<24) | ((uint64_t)p[1]<<16) | ((uint64_t)p[2]<<8) | p[3]; break;
        case 8: val = ((uint64_t)(b0&0x3F)<<56) | ((uint64_t)p[1]<<48) | ((uint64_t)p[2]<<40) | ((uint64_t)p[3]<<32)
                    | ((uint64_t)p[4]<<24) | ((uint64_t)p[5]<<16) | ((uint64_t)p[6]<<8) | p[7]; break;
    }
    *v=val; return len;
}

/* ===== SETTINGS/TP ===== */
void picowt_set_transport_parameters(picoquic_cnx_t* cnx)
{
    if (!cnx) return;
    picoquic_tp_t tp; memset(&tp, 0, sizeof(tp));
    tp.initial_max_data = 64*1024*1024;
    tp.initial_max_stream_data_uni = 16*1024*1024;
    tp.initial_max_stream_data_bidi_local = 16*1024*1024;
    tp.initial_max_stream_data_bidi_remote = 16*1024*1024;
    picoquic_set_transport_parameters(cnx, &tp);
}

/* ===== control stream ctx ===== */
h3zero_stream_ctx_t* picowt_set_control_stream(picoquic_cnx_t* cnx, h3zero_callback_ctx_t* h3_ctx)
{
    (void)cnx;
    h3zero_stream_ctx_t* sc = (h3zero_stream_ctx_t*)malloc(sizeof(*sc));
    if (!sc) return NULL;
    memset(sc, 0, sizeof(*sc));
    sc->path_callback_ctx = h3_ctx;
    sc->stream_id = 0; /* CONNECT 스트림 id는 외부에서 설정/사용 */
    return sc;
}

/* ===== local WT stream create ===== */
h3zero_stream_ctx_t* picowt_create_local_stream(picoquic_cnx_t* cnx, int is_bidir,
    h3zero_callback_ctx_t* h3_ctx, uint64_t control_stream_id)
{
    uint64_t sid = picoquic_get_next_local_stream_id(cnx, is_bidir);
    h3zero_stream_ctx_t* sc = (h3zero_stream_ctx_t*)malloc(sizeof(*sc));
    if (!sc) return NULL;
    memset(sc, 0, sizeof(*sc));
    sc->stream_id = sid;
    sc->path_callback_ctx = h3_ctx;

    if (!is_bidir) {
        /* WT unidirectional stream header: 0x54 + control_stream_id */
        uint8_t hdr[16]; size_t off=0;
        size_t n = wt_varint_encode(0x54, hdr+off, sizeof(hdr)-off); if (!n) { free(sc); return NULL; } off+=n;
        n = wt_varint_encode(control_stream_id, hdr+off, sizeof(hdr)-off);  if (!n) { free(sc); return NULL; } off+=n;
        if (picoquic_add_to_stream(cnx, sid, hdr, off, 0)!=0) { free(sc); return NULL; }
    }
    return sc;
}

/* --- 최소 QPACK 헤더블록(리터럴 이름/값) 인코더: :status: 200 한 줄만 --- */
static size_t qpack_min_status_200(uint8_t* out, size_t cap)
{
    uint8_t* p = out; uint8_t* max = out + cap;
    /* Section Prefix: RIC=0, DB=0 */
    size_t n = wt_varint_encode(0, p, (size_t)(max-p)); if (!n) return 0; p+=n;
    n = wt_varint_encode(0, p, (size_t)(max-p)); if (!n) return 0; p+=n;

    /* Literal Field Line With Literal Name, no indexing (0x10) */
    if (p>=max) return 0; *p++ = 0x10;

    const char* name=":status"; size_t nl=strlen(name);
    n = wt_varint_encode(nl, p, (size_t)(max-p)); if (!n) return 0; p+=n;
    if ((size_t)(max-p) < nl) return 0; memcpy(p, name, nl); p+=nl;

    const char* val="200"; size_t vl=3;
    n = wt_varint_encode(vl, p, (size_t)(max-p)); if (!n) return 0; p+=n;
    if ((size_t)(max-p) < vl) return 0; memcpy(p, val, vl); p+=vl;

    return (size_t)(p - out);
}
int h3_send_status_200_headers(picoquic_cnx_t* cnx, uint64_t stream_id)
{
    uint8_t block[256];
    size_t blen = qpack_min_status_200(block, sizeof(block));
    if (!blen) return -1;

    uint8_t frame[300]; uint8_t* p=frame; uint8_t* max=frame+sizeof(frame);
    size_t n = wt_varint_encode(0x1, p, (size_t)(max-p)); if (!n) return -1; p+=n;       /* HEADERS frame type */
    n = wt_varint_encode(blen, p, (size_t)(max-p)); if (!n) return -1; p+=n;             /* frame length */
    if ((size_t)(max-p) < blen) return -1; memcpy(p, block, blen); p+=blen;

    return picoquic_add_to_stream(cnx, stream_id, frame, (size_t)(p-frame), 0);
}

/* ===== CONNECT 응답(서버) ===== */
/* ===== CONNECT 응답(서버) ===== */
int picowt_connect(picoquic_cnx_t* cnx, h3zero_callback_ctx_t* h3_ctx, h3zero_stream_ctx_t* stream_ctx,
                   const char* authority, const char* path,
                   picohttp_post_data_cb_fn wt_callback, void* wt_ctx)
{
    (void)h3_ctx; (void)wt_callback; (void)wt_ctx;
    fprintf(stderr, "[WebTransport] CONNECT 수신: authority=%s path=%s sid=%" PRIu64 "\n",
            authority?authority:"(null)", path?path:"(null)", stream_ctx?stream_ctx->stream_id:0);

    if (!cnx || !stream_ctx) return -1;

    /* :status 200 HEADERS 전송 */
    if (h3_send_status_200_headers(cnx, stream_ctx->stream_id) != 0) {
        fprintf(stderr, "[WebTransport] 200 OK HEADERS 전송 실패\n");
        return -1;
    }

    /* 여기서 POST용 콜백(wt_callback) 호출하지 않음 */
    return 0;
}

/* ================== CLOSE / DRAIN 캡슐 송신 ================== */
int picowt_send_close_session_message(picoquic_cnx_t* cnx,
    h3zero_stream_ctx_t* control_stream_ctx, uint32_t picowt_err, const char* err_msg)
{
    if (!cnx || !control_stream_ctx) return -1;

    uint8_t buf[1024]; size_t off=0;
    size_t msg_len = err_msg ? strlen(err_msg) : 0;

    off += wt_varint_encode(picowt_capsule_close_webtransport_session, buf+off, sizeof(buf)-off);
    off += wt_varint_encode(4 + msg_len, buf+off, sizeof(buf)-off);
    if (sizeof(buf)-off < 4 + msg_len) return -1;

    buf[off++] = (uint8_t)(picowt_err>>24);
    buf[off++] = (uint8_t)(picowt_err>>16);
    buf[off++] = (uint8_t)(picowt_err>>8);
    buf[off++] = (uint8_t)(picowt_err);

    if (msg_len) { memcpy(buf+off, err_msg, msg_len); off += msg_len; }

    return picoquic_add_to_stream(cnx, control_stream_ctx->stream_id, buf, off, 0);
}

int picowt_send_drain_session_message(picoquic_cnx_t* cnx,
    h3zero_stream_ctx_t* control_stream_ctx)
{
    if (!cnx || !control_stream_ctx) return -1;
    uint8_t buf[16]; size_t off=0;
    off += wt_varint_encode(picowt_capsule_drain_webtransport_session, buf+off, sizeof(buf)-off);
    off += wt_varint_encode(0, buf+off, sizeof(buf)-off);
    return picoquic_add_to_stream(cnx, control_stream_ctx->stream_id, buf, off, 0);
}

/* ================== 캡슐 수신/해제 ================== */
int picowt_receive_capsule(picoquic_cnx_t* cnx, h3zero_stream_ctx_t* stream_ctx,
    const uint8_t* bytes, const uint8_t* bytes_max,
    picowt_capsule_t* capsule, h3zero_callback_ctx_t* h3_ctx)
{
    (void)cnx; (void)stream_ctx; (void)h3_ctx;
    if (!capsule || !bytes || bytes>=bytes_max) return -1;

    memset(capsule, 0, sizeof(*capsule));

    const uint8_t* p = bytes;
    uint64_t t=0, l=0;

    size_t n1 = wt_varint_decode(p, bytes_max, &t); if (!n1) return -1; p += n1;
    size_t n2 = wt_varint_decode(p, bytes_max, &l); if (!n2) return -1; p += n2;
    if ((uint64_t)(bytes_max - p) < l) return -1;

    if (t == picowt_capsule_close_webtransport_session) {
        if (l < 4) return -1;
        uint32_t ec = ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
        capsule->error_code = ec;
        if (l > 4) {
            capsule->error_msg = (const uint8_t*)(p+4);
            capsule->error_msg_len = (size_t)(l - 4);
        }
    } else if (t == picowt_capsule_drain_webtransport_session) {
        /* payload 없음 */
    } else {
        /* 기타 캡슐은 h3_capsule에 직접 채우지 않음(내부 정의 미노출) */
    }

    fprintf(stderr, "[WT] capsule RX: type=0x%llx len=%llu\n",
            (unsigned long long)t, (unsigned long long)l);
    return 0;
}


void picowt_release_capsule(picowt_capsule_t* capsule)
{
    /* 현재 error_msg는 외부 버퍼를 가리키므로 free하지 않음. */
    (void)capsule;
}

/* ================== deregister (no-op) ================== */
void picowt_deregister(picoquic_cnx_t* cnx, h3zero_callback_ctx_t* h3_ctx, h3zero_stream_ctx_t* control_stream_ctx)
{
    (void)cnx; (void)h3_ctx; (void)control_stream_ctx;
}
