// client/wt_callback.c  — safe & clean version
#include "wt_client.h"
#include <inttypes.h>
#include <stdio.h>

/* 이벤트 이름(간단 로그용) */
static const char* evname_short(picohttp_call_back_event_t ev) {
    switch (ev) {
    case picohttp_callback_connect:   return "connect";
    case picohttp_callback_post:      return "post";
    case picohttp_callback_post_data: return "post_data";
    case picohttp_callback_post_fin:  return "post_fin";
    default:                          return "other";
    }
}

/* 연결 상태 문자열 (외부 API에서 얻은 enum 값만 사용) */
static const char* picoquic_state_to_str(picoquic_state_enum st) {
    switch (st) {
    case picoquic_state_client_init:            return "client_init";
    case picoquic_state_client_init_sent:       return "client_init_sent";
    case picoquic_state_client_renegotiate:     return "client_renegotiate";
    case picoquic_state_client_retry_received:  return "client_retry_received";
    case picoquic_state_client_init_resent:     return "client_init_resent";
    case picoquic_state_client_ready_start:     return "client_ready_start";
    case picoquic_state_server_init:            return "server_init";
    case picoquic_state_ready:                  return "ready";
    case picoquic_state_disconnecting:          return "disconnecting";
    case picoquic_state_closing_received:       return "closing_received";
    case picoquic_state_closing:                return "closing";
    case picoquic_state_draining:               return "draining";
    case picoquic_state_disconnected:           return "disconnected";
    default:                                    return "unknown";
    }
}

/* 메인 콜백 */
int client_cb(picoquic_cnx_t* cnx,
              uint8_t* bytes, size_t length,
              picohttp_call_back_event_t event,
              h3zero_stream_ctx_t* h3s,
              void* app_v)
{
    app_ctx_t* app = (app_ctx_t*)app_v;

    LOGV("ev=%s sid=%" PRIu64 " len=%zu",
         evname_short(event), h3s ? h3s->stream_id : 0, length);

    switch (event) {
    case picohttp_callback_connect: {
        if (!h3s) return 0;

        /* 핸드셰이크 상태 로깅(공개 API만 사용) */
        picoquic_state_enum st = picoquic_get_cnx_state(cnx);
        fprintf(stderr, "[TLS] state=%s(%d)\n", picoquic_state_to_str(st), (int)st);

        /* WT 세션의 컨트롤 스트림 ID로 사용 */
        app->session_ctrl_id = h3s->stream_id;

        /* 초기 제어 메시지 전송 */
        (void)send_wt_control(cnx, app->session_ctrl_id, CTRL_START, NULL, 0);

        uint8_t fps_be[2] = {
            (uint8_t)(DEFAULT_FPS >> 8),
            (uint8_t)(DEFAULT_FPS & 0xFF)
        };
        (void)send_wt_control(cnx, app->session_ctrl_id, CTRL_SET_FPS, fps_be, sizeof(fps_be));

        uint8_t br_be[4] = {
            (uint8_t)((DEFAULT_BITRATE >> 24) & 0xFF),
            (uint8_t)((DEFAULT_BITRATE >> 16) & 0xFF),
            (uint8_t)((DEFAULT_BITRATE >>  8) & 0xFF),
            (uint8_t)( DEFAULT_BITRATE        & 0xFF)
        };
        (void)send_wt_control(cnx, app->session_ctrl_id, CTRL_SET_BITRATE, br_be, sizeof(br_be));
        break;
    }

    /* 서버가 이 경로로 데이터를 밀어줄 때 */
    case picohttp_callback_post_data:
    case picohttp_callback_post_fin: {
        if (!h3s || bytes == NULL || length == 0) break;
        (void)wt_handle_stream_data(app, h3s->stream_id, bytes, length);
        break;
    }

    default:
        /* 필요시 다른 이벤트도 추가 처리 가능 */
        break;
    }

    return 0;
}
