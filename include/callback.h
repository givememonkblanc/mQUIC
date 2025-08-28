#ifndef CALLBACK_H
#define CALLBACK_H

#include "picoquic.h" // picoquic 타입(picoquic_cnx_t 등)을 사용하기 위해 필수

/*
 * QUIC 연결의 모든 이벤트를 처리할 메인 콜백 함수를 선언합니다.
 * 이 함수의 실제 내용은 callback.c 파일에 있습니다.
 */
int my_server_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t event, void* callback_context, void* v_stream_ctx);

#endif // CALLBACK_H