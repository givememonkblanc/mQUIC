#include "h3zero_client.h"
#include "h3zero_qpack.h"    // ← QPACK encoder 선언
#include "picoquic.h"
#include <stdio.h>
#include <string.h>

int h3zero_client_create_connect_request(
    picoquic_cnx_t* cnx,
    uint64_t stream_id,
    const char* server_name,
    const char* path,
    int fin)
{
    if (!cnx || !server_name || !path) return -1;

    uint8_t buf[1024];
    uint8_t* bytes     = buf;
    uint8_t* bytes_max = buf + sizeof(buf);

    /* QPACK 헤더 블록 작성 */
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

    size_t header_len = bytes - buf;
    if (header_len == 0 || header_len > 1024) return -1;

    /* HEADERS 프레임 생성 */
    uint8_t frame[1050];
    uint8_t* fptr = frame;
    /* 0x01 = HEADERS */
    fptr += picoquic_varint_encode(0x01, fptr);
    fptr += picoquic_varint_encode(header_len, fptr);
    memcpy(fptr, buf, header_len);
    fptr += header_len;

    /* 스트림에 추가 */
    int ret = picoquic_add_to_stream(cnx, stream_id,
                                     frame,
                                     (size_t)(fptr - frame),
                                     fin ? 1 : 0);
    return ret;
}
