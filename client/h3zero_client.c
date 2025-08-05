#include "h3zero_client.h"
#include <stdio.h>
#include <string.h>

#include "h3zero.h"
#include "picoquic.h"

int h3zero_client_create_connect_request(
    picoquic_cnx_t* cnx,
    uint64_t stream_id,
    const char* server_name,
    const char* path,
    int fin)
{
    if (!cnx || !server_name || !path) return -1;

    uint8_t buf[1024];
    uint8_t* bytes = buf;
    uint8_t* bytes_max = buf + sizeof(buf);

    /* QPACK 헤더 블록 작성 */
    bytes = h3zero_qpack_enc_header(bytes, bytes_max, ":method", "CONNECT");
    bytes = h3zero_qpack_enc_header(bytes, bytes_max, ":protocol", "webtransport");
    bytes = h3zero_qpack_enc_header(bytes, bytes_max, ":scheme", "https");
    bytes = h3zero_qpack_enc_header(bytes, bytes_max, ":path", path);
    bytes = h3zero_qpack_enc_header(bytes, bytes_max, ":authority", server_name);

    size_t header_len = bytes - buf;

    /* HEADERS 프레임 생성 */
    uint8_t frame[1050];
    uint8_t* frame_bytes = frame;
    frame_bytes += picoquic_varint_encode(0x01, frame_bytes);         // HEADERS frame type
    frame_bytes += picoquic_varint_encode(header_len, frame_bytes);   // Length
    memcpy(frame_bytes, buf, header_len);
    frame_bytes += header_len;

    /* 전송 */
    return picoquic_add_to_stream(cnx, stream_id, frame, frame_bytes - frame, fin);
}
