#include "h3zero_client.h"
#include "h3zero_qpack.h"    // ← QPACK encoder 선언
#include "picoquic.h"
#include "picoquic_internal.h"
#include <stdio.h>
#include <string.h>
static inline int write_varint(uint8_t **p, uint8_t *end, uint64_t v) {
    if (*p >= end) return -1;
    size_t w = picoquic_varint_encode(*p, (size_t)(end - *p), v);
    if (w == 0) return -1;  // no space
    *p += w;
    return 0;
}

int h3zero_client_create_connect_request(
    picoquic_cnx_t* cnx,
    uint64_t stream_id,
    const char* server_name,
    const char* path,
    int fin)
{
    if (!cnx || !server_name || !path) return -1;

    /* QPACK 헤더 블록 생성 */
    uint8_t hbuf[1024];
    uint8_t* bytes     = hbuf;
    uint8_t* bytes_max = hbuf + sizeof(hbuf);

    bytes = h3zero_qpack_enc_header(bytes, bytes_max, ":method",    "CONNECT");
    if (bytes == NULL) return -1;
    bytes = h3zero_qpack_enc_header(bytes, bytes_max, ":protocol",  "webtransport");
    if (bytes == NULL) return -1;
    bytes = h3zero_qpack_enc_header(bytes, bytes_max, ":scheme",    "https");
    if (bytes == NULL) return -1;
    bytes = h3zero_qpack_enc_header(bytes, bytes_max, ":path",      path);
    if (bytes == NULL) return -1;
    bytes = h3zero_qpack_enc_header(bytes, bytes_max, ":authority", server_name);
    if (bytes == NULL) return -1;

    size_t header_len = (size_t)(bytes - hbuf);
    if (header_len == 0 || header_len > sizeof(hbuf)) return -1;

    /* H3 HEADERS 프레임: type=0x01, length=header_len, payload=header_block */
    uint8_t frame[1050];
    uint8_t* fptr = frame;
    uint8_t* fend = frame + sizeof(frame);

    if (write_varint(&fptr, fend, 0x01) != 0) return -1;                    // type
    if (write_varint(&fptr, fend, (uint64_t)header_len) != 0) return -1;    // length
    if ((size_t)(fend - fptr) < header_len) return -1;                      // space check
    memcpy(fptr, hbuf, header_len);
    fptr += header_len;

    return picoquic_add_to_stream(cnx, stream_id, frame, (size_t)(fptr - frame), fin ? 1 : 0);
}

int h3zero_create_headers_frame(
    picoquic_cnx_t* cnx,
    uint64_t stream_id,
    const uint8_t* headers,
    size_t headers_len,
    int fin)
{
    if (!cnx || !headers || headers_len == 0) return -1;

    uint8_t frame[1200];
    uint8_t* fptr = frame;
    uint8_t* fend = frame + sizeof(frame);

    // frame type = 0x01 (HEADERS)
    if (write_varint(&fptr, fend, 0x01) != 0) return -1;
    // length
    if (write_varint(&fptr, fend, headers_len) != 0) return -1;
    // copy payload
    if ((size_t)(fend - fptr) < headers_len) return -1;
    memcpy(fptr, headers, headers_len);
    fptr += headers_len;

    return picoquic_add_to_stream(cnx, stream_id, frame, (size_t)(fptr - frame), fin ? 1 : 0);
}