#pragma once
#include "picoquic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HTTP/3 CONNECT 요청 생성 (WebTransport 세션)
 * cnx         : picoquic 연결 핸들
 * stream_id   : CONNECT 요청을 보낼 bidirectional stream ID
 * server_name : :authority 헤더 값
 * path        : :path 헤더 값 (예: "/camera")
 * fin         : 전송 완료 시 1, 더 보낼 데이터가 있으면 0
 */
int h3zero_client_create_connect_request(
    picoquic_cnx_t* cnx,
    uint64_t stream_id,
    const char* server_name,
    const char* path,
    int fin);

#ifdef __cplusplus
}
#endif
