#include "camera.h"
#include "logger.h" // 로깅 모듈 포함
#include "qlog.h"
#include "picoquic_binlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <autoqlog.h>
#include <pthread.h>
#include <picoquic.h>
#include <picoquic_utils.h>
#include <picoquic_packet_loop.h>

// 애플리케이션의 상태와 리소스를 관리하는 구조체
typedef struct {
    camera_handle_t cam_handle;
    unsigned char* frame_buffer;
    size_t frame_buffer_size;
    int is_sending;
    int frame_count;
    int max_frames;

    picoquic_cnx_t* cnx;
    uint64_t stream_id;
    void* stream_ctx;
    int frame_interval_usec;
    pthread_t scheduler_thread;
} streamer_context_t;

// 프레임 전송을 위한 루프 스레드
void* streaming_loop(void* arg) {
    streamer_context_t* ctx = (streamer_context_t*)arg;
    log_write("LOOP: Streaming thread started.");
    while (ctx->is_sending) {
        log_write("LOOP: Marking stream active (stream_id=%llu)...", ctx->stream_id);
        picoquic_mark_active_stream(ctx->cnx, ctx->stream_id, 1, ctx->stream_ctx);
        usleep(ctx->frame_interval_usec);
    }
    log_write("LOOP: Streaming thread exiting.");
    return NULL;
}

// Picoquic 콜백 함수
int streamer_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
    streamer_context_t* ctx = (streamer_context_t*)callback_ctx;

    if (length > 0 && strncmp((char*)bytes, "START", length) == 0) {
        log_write("EVENT: Received 'START' from client. Starting transmission on stream %llu.", stream_id);
        ctx->is_sending = 1;
        ctx->frame_count = 0;
        ctx->cnx = cnx;
        ctx->stream_id = stream_id;
        ctx->stream_ctx = v_stream_ctx;
        pthread_create(&ctx->scheduler_thread, NULL, streaming_loop, ctx);
        return 0;
    }

    switch (fin_or_event) {
        case picoquic_callback_stream_reset:
        case picoquic_callback_stop_sending:
        case picoquic_callback_close:
            log_write("EVENT: Connection closed or reset. Stopping transmission.");
            ctx->is_sending = 0;
            pthread_join(ctx->scheduler_thread, NULL);
            if (v_stream_ctx != NULL) { free(v_stream_ctx); }
            break;

        case picoquic_callback_ready:
            log_write("EVENT: Stream %llu is ready. Waiting for 'START' request from client.", stream_id);
            break;

        case picoquic_callback_prepare_to_send:
            log_write("CALLBACK: prepare_to_send triggered for stream %llu.", stream_id);
            if (!ctx->is_sending) break;

            if (ctx->frame_count >= ctx->max_frames) {
                log_write("INFO: Max frame count reached. Stopping transmission.");
                ctx->is_sending = 0;
                break;
            }

            int jpeg_size = camera_capture_jpeg(ctx->cam_handle, ctx->frame_buffer, ctx->frame_buffer_size);
            int ret = -1;  // ✅ 여기에 선언 추가

            if (jpeg_size > 0) {
                int ret = picoquic_add_to_stream(cnx, stream_id, ctx->frame_buffer, jpeg_size, 0);
                if (ret == 0) {
                    ctx->frame_count++;
                    log_write("SENT: Frame %d (%d bytes) on stream %llu.", ctx->frame_count, jpeg_size, stream_id);
                } else {
                    log_write("FATAL: picoquic_add_to_stream failed with error %d! Stopping transmission.", ret);
                    ctx->is_sending = 0;
                }
            } else {
                log_write("ERROR: Failed to add to stream (ret=%d)", ret);
                ctx->is_sending = 0;
            }
            break;

        default:
            break;
    }
    return 0;
}

int run_server() {
    log_init("streamer_log.txt");
    log_write("INFO: Streamer starting up...");

    streamer_context_t streamer_ctx = {0};
    int ret = 0;

    streamer_ctx.frame_buffer_size = 1024 * 1024;
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

    streamer_ctx.max_frames = 1000000; // 사실상 무한 전송
    streamer_ctx.frame_interval_usec = 100000; // 10fps

    log_write("INFO: Camera and frame buffer are ready.");

    picoquic_quic_t* quic = NULL;
    const char* server_cert_file = "cert.pem";
    const char* server_key_file = "key.pem";
    int server_port = 4433;

    quic = picoquic_create(16, server_cert_file, server_key_file, NULL, "mjpeg-stream",
                           streamer_callback, &streamer_ctx, NULL,NULL, NULL,
                           picoquic_current_time(), NULL, NULL, NULL, 0);

    if (quic == NULL) {
        log_write("FATAL: Failed to create picoquic context.");
        ret = -1;
    } else {
        picoquic_set_binlog(quic, "binlog");
        picoquic_set_qlog(quic, "qlogs");
        picoquic_use_unique_log_names(quic, 1);
        log_write("INFO: QUIC server waiting for connections on port %d...", server_port);
        ret = picoquic_packet_loop(quic, server_port, 0, 0, 0, 0, NULL, NULL);
        log_write("INFO: Packet loop finished with code %d.", ret);
    }

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