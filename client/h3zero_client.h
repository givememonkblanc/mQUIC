// client/h3zero_client.h
#ifndef H3ZERO_CLIENT_H
#define H3ZERO_CLIENT_H

#include <stdint.h>
#include "picoquic.h"

// 선언만! 몸체는 절대 넣지 마세요.
int h3zero_client_create_connect_request(
    picoquic_cnx_t* cnx,
    uint64_t        stream_id,
    const char*     server_name,
    const char*     path,
    int             fin);
int h3zero_create_headers_frame(picoquic_cnx_t* cnx, uint64_t stream_id, const uint8_t* headers, size_t headers_len, int fin);

#endif // H3ZERO_CLIENT_H

