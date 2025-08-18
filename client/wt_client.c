/* client/wt_client.c  —  cleaned & consolidated */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <signal.h>      /* sig_atomic_t */
#include <sys/stat.h>    /* mkdir */
#include <sys/types.h>

#include "wt_client.h"   /* public types & prototypes (includes picoquic/picotls) */

/* ============================ Globals & Macros ============================ */

volatile sig_atomic_t g_running = 1;   /* packet loop gate */

#ifndef LOGV
#define LOGV(fmt, ...) do { fprintf(stderr, "[client] " fmt "\n", ##__VA_ARGS__); } while (0)
#endif

/* frame size sanity for length-framing; huge = treat as SOI/EOI stream */
#ifndef WT_MAX_FRAME_LEN
#define WT_MAX_FRAME_LEN (32u * 1024u * 1024u) /* 32 MB */
#endif

/* ======================= Internal per-stream state ======================== */

struct wt_stream_t {
    uint64_t stream_id;

    uint8_t* buf;
    size_t   cap;
    size_t   got;

    int      header_ok;       /* seen WT header? (0x54 + session_id) */
    uint64_t cur_len;         /* declared frame length (may include PTS varint) */
    size_t   pts_len;         /* bytes consumed by PTS varint if present */
    int      use_len_framing; /* once detected, stick to length-framing */

    struct wt_stream_t* next;
};

/* small helpers */
static inline size_t payload_need(const struct wt_stream_t* st)
{
    return (st->cur_len > st->pts_len) ? (size_t)(st->cur_len - st->pts_len) : 0u;
}

/* forward decls for local helpers */
static void on_jpeg_found_cb(const uint8_t* data, size_t len, void* user);

/* ============================= Public helpers ============================ */

int ensure_dir(const char* path)
{
    if (mkdir(path, 0775) == 0) return 0;
    return (errno == EEXIST) ? 0 : -1;
}

/* RFC 9000 varint (decode/encode) */
size_t quic_varint_decode(const uint8_t* p, size_t len, uint64_t* out)
{
    if (len == 0) return 0;
    uint8_t b0 = p[0];
    switch (b0 & 0xC0) {
    case 0x00: if (len < 1) return 0; *out = b0 & 0x3F; return 1;
    case 0x40: if (len < 2) return 0; *out = ((uint64_t)(b0 & 0x3F) << 8) | p[1]; return 2;
    case 0x80:
        if (len < 4) return 0;
        *out = ((uint64_t)(b0 & 0x3F) << 24) | ((uint64_t)p[1] << 16) | ((uint64_t)p[2] << 8) | p[3];
        return 4;
    default:
        if (len < 8) return 0;
        *out = ((uint64_t)(b0 & 0x3F) << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) |
               ((uint64_t)p[3] << 32) | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
               ((uint64_t)p[6] << 8)  | p[7];
        return 8;
    }
}

size_t quic_varint_encode(uint64_t v, uint8_t* out)
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
    } else {
        out[0] = 0xC0 | (uint8_t)(v >> 56);
        out[1] = (uint8_t)(v >> 48);
        out[2] = (uint8_t)(v >> 40);
        out[3] = (uint8_t)(v >> 32);
        out[4] = (uint8_t)(v >> 24);
        out[5] = (uint8_t)(v >> 16);
        out[6] = (uint8_t)(v >> 8);
        out[7] = (uint8_t)v; return 8;
    }
}

/* ALPN selector: prefer "h3" */
size_t alpn_select(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count)
{
    (void)quic;
    static const char* H3 = "h3";
    for (size_t i = 0; i < count; i++) {
        if (list[i].len == strlen(H3) && memcmp(list[i].base, H3, list[i].len) == 0) {
            return i;
        }
    }
    return count; /* no match => let picoquic fail handshake */
}

/* packet loop hook for graceful stop */
int loop_hook(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum mode, void* ctx, void* arg)
{
    (void)quic; (void)mode; (void)ctx; (void)arg;
    return g_running ? 0 : PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
}

/* send a small WT control message on a new client-uni stream */
int send_wt_control(picoquic_cnx_t* cnx, uint64_t session_id,
                    uint8_t code, const uint8_t* payload, size_t payload_len)
{
    uint64_t sid = picoquic_get_next_local_stream_id(cnx, 1); /* client uni */
    if ((sid & 0x3) != 0x2) return -1;   // ← 이렇게 수정
    uint8_t hdr[16]; size_t off = 0;
    off += quic_varint_encode(0x54, hdr + off);
    off += quic_varint_encode(session_id, hdr + off);

    if (picoquic_add_to_stream(cnx, sid, hdr, off, 0) != 0) return -1;
    if (picoquic_add_to_stream(cnx, sid, &code, 1, payload_len == 0 ? 1 : 0) != 0) return -1;
    if (payload_len > 0) {
        if (picoquic_add_to_stream(cnx, sid, payload, payload_len, 1) != 0) return -1;
    }
    LOGV("sent ctrl (sid=%" PRIu64 ", code=0x%02X, pay=%zu)", sid, code, payload_len);
    return 0;
}

/* ======================= JPEG & file I/O small helpers ==================== */

void save_frame(app_ctx_t* app, const uint8_t* data, size_t len)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/frame_%05d.jpg", app->out_dir, app->saved);
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[client] fopen(%s) failed: %s\n", path, strerror(errno)); return; }
    fwrite(data, 1, len, f);
    fclose(f);
    fprintf(stdout, "[client] saved %s (%zu bytes)\n", path, len);
    app->saved++;
}

static void on_jpeg_found_cb(const uint8_t* data, size_t len, void* user)
{
    save_frame((app_ctx_t*)user, data, len);
}

void extract_jpegs(uint8_t* buf, size_t* len_io,
                   void (*on_frame)(const uint8_t*, size_t, void*), void* user)
{
    static const uint8_t SOI[2] = {0xFF,0xD8}, EOI[2] = {0xFF,0xD9};
    uint8_t* b = buf; size_t L = *len_io;

    for (;;) {
        size_t s = 0; while (s+1 < L && !(b[s]==SOI[0] && b[s+1]==SOI[1])) s++;
        if (s + 1 >= L) {
            if (L > 64*1024) { memmove(b, b + (L-64*1024), 64*1024); L = 64*1024; }
            *len_io = L; return;
        }
        if (s > 0) { memmove(b, b + s, L - s); L -= s; }

        size_t e = 2; while (e+1 < L && !(b[e]==EOI[0] && b[e+1]==EOI[1])) e++;
        if (e + 1 >= L) { *len_io = L; return; }

        if (on_frame) on_frame(b, e+2, user);
        else          on_jpeg_found_cb(b, e+2, user);

        memmove(b, b + e + 2, L - (e + 2)); L -= (e + 2);
    }
}

/* =========================== Stream list helpers ========================== */

wt_stream_t* get_stream(app_ctx_t* app, uint64_t sid)
{
    for (wt_stream_t* p = app->streams; p; p = p->next)
        if (p->stream_id == sid) return p;

    wt_stream_t* p = (wt_stream_t*)calloc(1, sizeof(*p));
    if (!p) return NULL;

    p->stream_id = sid;
    p->cap = 1 << 20;
    p->buf = (uint8_t*)malloc(p->cap);
    if (!p->buf) { free(p); return NULL; }

    p->next = app->streams;
    app->streams = p;
    return p;
}

void free_streams(app_ctx_t* app)
{
    wt_stream_t* p = app->streams;
    while (p) { wt_stream_t* n = p->next; free(p->buf); free(p); p = n; }
    app->streams = NULL;
}

/* ========================= Main stream data handler ======================= */

int wt_handle_stream_data(app_ctx_t* app, uint64_t sid, uint8_t* bytes, size_t length)
{
    wt_stream_t* st = get_stream(app, sid);
    if (!st) return -1;

    size_t off = 0;

    while (off < length) {
        /* A) one-time WT uni header: 0x54 + session_id */
        if (!st->header_ok) {
            uint64_t v = 0, sidv = 0;
            size_t c_type = quic_varint_decode(bytes + off, length - off, &v);
            if (c_type == 0) break;

            size_t c_sid  = quic_varint_decode(bytes + off + c_type, length - off - c_type, &sidv);
            if (c_sid == 0) break;

            off += c_type + c_sid;

            if (v != 0x54) {
                LOGV("bad WT uni type=0x%" PRIx64, v);
                return -1;
            }
            if (sidv != app->session_ctrl_id) {
                LOGV("WT session id mismatch: got=%" PRIu64 ", want=%" PRIu64, sidv, app->session_ctrl_id);
                return -1;
            }
            st->header_ok       = 1;
            st->cur_len         = 0;
            st->got             = 0;
            st->pts_len         = 0;
            st->use_len_framing = 0;
            LOGV("WT header OK (stream=%" PRIu64 ")", sid);
            continue;
        }

        /* B) try to detect length-framing (first time only) */
        if (!st->use_len_framing && st->cur_len == 0) {
            if (off >= length) break;

            /* ensure we have at least the varint for length */
            size_t need_len = 1u << ((bytes[off] & 0xC0) >> 6);
            if (length - off < need_len) break;

            uint64_t fl = 0;
            size_t c = quic_varint_decode(bytes + off, length - off, &fl);
            if (c == 0) break;

            if (fl == 0 || fl > WT_MAX_FRAME_LEN) {
                /* fallback to SOI/EOI mode */
                size_t chunk = length - off;
                if (st->cap < st->got + chunk) {
                    size_t need_cap = st->got + chunk;
                    uint8_t* nb = (uint8_t*)realloc(st->buf, need_cap);
                    if (!nb) return -1;
                    st->buf = nb; st->cap = need_cap;
                }
                memcpy(st->buf + st->got, bytes + off, chunk);
                st->got += chunk;
                off = length;

                size_t L = st->got;
                extract_jpegs(st->buf, &L, on_jpeg_found_cb, app);
                if (app->max_frames > 0 && app->saved >= app->max_frames) g_running = 0;
                st->got = L;
                break;
            }

            /* adopt length-framing */
            off += c;
            st->cur_len         = fl;
            st->got             = 0;
            st->pts_len         = 0;
            st->use_len_framing = 1;

            /* optional PTS */
            uint64_t dummy = 0;
            size_t c2 = quic_varint_decode(bytes + off, length - off, &dummy);
            if (c2 > 0) { off += c2; st->pts_len = c2; }

            size_t need_payload = payload_need(st);
            if (need_payload > st->cap) {
                uint8_t* nb = (uint8_t*)realloc(st->buf, need_payload);
                if (!nb) return -1;
                st->buf = nb; st->cap = need_payload;
            }
            LOGV("len-framing detected: frame_len=%" PRIu64 " (pts_len=%zu)", st->cur_len, st->pts_len);
            /* fall through to copy payload */
        }

        /* C) receive payload in length-framing mode */
        if (st->use_len_framing) {
            size_t need = payload_need(st);
            if (need == 0) { /* defensive: if mis-declared, fallback next */
                st->use_len_framing = 0;
                continue;
            }

            size_t avail = length - off;
            size_t take  = (need - st->got < avail) ? (need - st->got) : avail;
            if (take > 0) {
                memcpy(st->buf + st->got, bytes + off, take);
                st->got += take;
                off     += take;
            }

            if (st->got == need) {
                save_frame(app, st->buf, need);
                if (app->max_frames > 0 && app->saved >= app->max_frames) {
                    g_running = 0;
                    return 0;
                }
                st->cur_len = 0;
                st->got     = 0;
                st->pts_len = 0;
            }
            continue;
        }

        /* D) SOI/EOI accumulation & extraction */
        {
            size_t chunk = length - off;
            if (st->cap < st->got + chunk) {
                size_t need_cap = st->got + chunk;
                uint8_t* nb = (uint8_t*)realloc(st->buf, need_cap);
                if (!nb) return -1;
                st->buf = nb; st->cap = need_cap;
            }
            memcpy(st->buf + st->got, bytes + off, chunk);
            st->got += chunk;
            off = length;

            size_t L = st->got;
            extract_jpegs(st->buf, &L, on_jpeg_found_cb, app);
            if (app->max_frames > 0 && app->saved >= app->max_frames) g_running = 0;
            st->got = L;
            break;
        }
    }
    return 0;
}
int h3zero_client_create_connect_request(picoquic_cnx_t* cnx,
                                         uint64_t stream_id,
                                         const char* authority,
                                         const char* path,
                                         int is_webtransport)
{
    /* 1) QPACK 헤더블록 만들기 (CONNECT + :protocol=webtransport + :authority + :path) */
    uint8_t hb[1024];
    uint8_t* p = h3zero_create_connect_header_frame(
        hb, hb + sizeof(hb),
        authority,
        (const uint8_t*)path, strlen(path),
        is_webtransport ? "webtransport" : NULL,  /* :protocol */
        NULL,                                      /* origin(필요 없으면 NULL) */
        H3ZERO_USER_AGENT_STRING                   /* UA */
    );
    if (p == NULL) return -1;
    size_t hb_len = (size_t)(p - hb);

    /* 2) HTTP/3 HEADERS 프레임 래핑 (type=0x01, 길이는 QUIC varint) */
    uint8_t frame[16 + sizeof(hb)];
    size_t off = 0;
    off += quic_varint_encode(0x01, frame + off);     /* HEADERS */
    off += quic_varint_encode(hb_len, frame + off);   /* length */
    memcpy(frame + off, hb, hb_len);
    off += hb_len;

    /* 3) 스트림으로 전송 (FIN은 내지 않음) */
    return picoquic_add_to_stream(cnx, stream_id, frame, off, 0);
}