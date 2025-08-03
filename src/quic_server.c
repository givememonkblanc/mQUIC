#include "camera.h"
#include "logger.h" // 로깅 모듈 포함

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <picoquic.h>
#include <picoquic_utils.h>
#include <picoquic_packet_loop.h>

// 애플리케이션의 상태와 리소스를 관리하는 구조체
typedef struct {
    camera_handle_t cam_handle;      // 카메라 핸들
    unsigned char* frame_buffer;     // JPEG 프레임을 담을 재사용 버퍼
    size_t frame_buffer_size;        // 버퍼의 크기
    int is_sending;                  // 현재 데이터 전송 중인지 여부
    int frame_count;                 // 보낸 프레임 수
    int max_frames;                  // 전송할 최대 프레임 수
} streamer_context_t;

/**
 * @brief Picoquic의 이벤트 콜백 함수
 */
/**
 * @brief Picoquic의 이벤트 콜백 함수 (에러 확인 최종 버전)
 */
int streamer_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
    streamer_context_t* ctx = (streamer_context_t*)callback_ctx;

    // "START" 요청 처리 로직
    if (length > 0) {
        if (strncmp((char*)bytes, "START", length) == 0) {
            log_write("EVENT: Received 'START' from client. Starting transmission on stream %llu.", stream_id);
            ctx->is_sending = 1;
            ctx->frame_count = 0;
            picoquic_mark_active_stream(cnx, stream_id, 1, v_stream_ctx);
            return 0;
        }
    }

    switch (fin_or_event) {
        case picoquic_callback_stream_reset:
        case picoquic_callback_stop_sending:
        case picoquic_callback_close:
            log_write("EVENT: Connection closed or reset. Stopping transmission.");
            ctx->is_sending = 0;
            if (v_stream_ctx != NULL) { free(v_stream_ctx); }
            break;

        case picoquic_callback_ready:
            log_write("EVENT: Stream %llu is ready. Waiting for 'START' request from client.", stream_id);
            break;

        case picoquic_callback_prepare_to_send:
            if (!ctx->is_sending) {
                break;
            }

            int jpeg_size = camera_capture_jpeg(ctx->cam_handle, ctx->frame_buffer, ctx->frame_buffer_size);

            if (jpeg_size > 0) {
                // ▼▼▼▼▼ 이 부분이 수정되었습니다 ▼▼▼▼▼
                // picoquic_add_to_stream의 반환 값을 확인합니다.
                int ret = picoquic_add_to_stream(cnx, stream_id, ctx->frame_buffer, jpeg_size, 0);

                if (ret == 0) {
                    // 성공한 경우에만 로그를 남기고 다음 전송을 예약합니다.
                    log_write("SENT: Frame %d (%d bytes) on stream %llu.",
                              ctx->frame_count + 1, jpeg_size, stream_id);
                    ctx->frame_count++;
                    picoquic_mark_active_stream(cnx, stream_id, 1, v_stream_ctx);
                } else {
                    // 실패 시 에러 로그를 남깁니다.
                    log_write("FATAL: picoquic_add_to_stream failed with error %d! Stopping transmission.", ret);
                    ctx->is_sending = 0; // 전송 중단
                }
                // ▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲

            } else {
                log_write("ERROR: Failed to capture JPEG frame from camera.");
                picoquic_mark_active_stream(cnx, stream_id, 1, v_stream_ctx);
            }
            break;

        default:
            break;
    }
    return 0;
}
/**
 * @brief 스트리밍 서버를 초기화하고 실행하는 메인 함수
 */
int run_server() {
    // 1. 로거 및 컨텍스트 초기화
    log_init("streamer_log.txt");
    log_write("INFO: Streamer starting up...");

    streamer_context_t streamer_ctx = {0};
    int ret = 0;

    // 2. 리소스 할당 (카메라, 프레임 버퍼)
    streamer_ctx.frame_buffer_size = 1024 * 1024; // 1MB 버퍼
    streamer_ctx.frame_buffer = (unsigned char*)malloc(streamer_ctx.frame_buffer_size);
    if (streamer_ctx.frame_buffer == NULL) {
        log_write("FATAL: Failed to allocate frame buffer.");
        log_close();
        return -1;
    }

    streamer_ctx.cam_handle = camera_create();
    if (streamer_ctx.cam_handle == NULL) {
        log_write("FATAL: Failed to initialize camera.");
        free(streamer_ctx.frame_buffer);
        log_close();
        return -1;
    }
    log_write("INFO: Camera and frame buffer are ready.");

    // 3. Picoquic 서버 설정
    picoquic_quic_t* quic = NULL;
    const char* server_cert_file = "cert.pem"; // 실제 인증서 파일 경로
    const char* server_key_file = "key.pem";   // 실제 키 파일 경로
    int server_port = 4433;

    quic = picoquic_create(16, server_cert_file, server_key_file, NULL, "mjpeg-stream",
                           streamer_callback, &streamer_ctx, NULL, NULL, NULL,
                           picoquic_current_time(), NULL, NULL, NULL, 0);

    if (quic == NULL) {
        log_write("FATAL: Failed to create picoquic context.");
        ret = -1;
    } else {
        log_write("INFO: QUIC server waiting for connections on port %d...", server_port);
        // 4. 서버 패킷 루프 실행
        ret = picoquic_packet_loop(quic, server_port, 0, 0, 0, 0, NULL, NULL);
        log_write("INFO: Packet loop finished with code %d.", ret);
    }

    // 5. 모든 리소스 정리
    log_write("INFO: Cleaning up resources...");
    if (quic != NULL) {
        picoquic_free(quic);
    }
    camera_destroy(streamer_ctx.cam_handle);
    free(streamer_ctx.frame_buffer);
    log_write("INFO: Streamer shut down.");
    log_close();

    return ret;
}