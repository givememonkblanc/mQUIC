#include "callback.h" // 방금 만든 헤더 파일을 포함
#include <stdio.h>

// callback.h에 선언했던 함수의 실제 내용을 정의합니다.
int my_server_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t event, void* callback_context, void* v_stream_ctx)
{
    // 이벤트 종류에 따라 다른 동작을 수행합니다.
    switch (event) {
        case picoquic_callback_stream_data:
            // 여기에 스트림 데이터 수신 시 처리할 로직을 집중적으로 구현합니다.
            printf("[Callback] 스트림 #%llu 에서 데이터 %zu 바이트 수신\n", (unsigned long long)stream_id, length);
            // 예: if (stream_id == 3) { handle_control_stream(...); }
            break;

        case picoquic_callback_close:
            // 연결이 종료될 때의 처리
            printf("[Callback] 연결이 종료됩니다.\n");
            // 여기에 리소스 정리 로직을 넣을 수 있습니다.
            break;
        
        // 필요한 다른 이벤트들을 처리하는 case를 추가합니다.
        // case picoquic_call_back_ready: ...
        
        default:
            // 처리하지 않는 이벤트는 무시
            break;
    }

    return 0;
}