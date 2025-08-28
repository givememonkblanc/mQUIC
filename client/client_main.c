#include "autoqlog.h"
#include "picoquic_binlog.h"
#include "picoquic.h"
#include "picoquic_packet_loop.h"
#include "picoquic_utils.h"
#include "qlog.h"
#include "h3zero.h"
#include "h3zero_protocol.h" 
#include "picotls.h"
#include "h3zero_client.h" // ✅ [수정] picohttp_header_t 및 관련 함수를 위해 추가
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h> // PRIu64 매크로

/* ────────────────────── 타입 및 전역 변수 정의 ────────────────────── */
static void print_usage(const char* app)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  --host <name>           SNI/Host (예: example.com 또는 127.0.0.1)\n"
        "  --addr <ip/host>        연결할 실제 주소(미지정 시 --host 사용)\n"
        "  --port <num>            UDP 포트 (기본: 4433)\n"
        "  --path <path>           WebTransport 엔드포인트 (기본: /)\n"
        "  --alpn <proto>          ALPN (기본: h3)\n"
        "  --sni <name>            SNI 별도 지정(기본: --host)\n"
        "  --no-verify             인증서 검증 생략(개발/테스트용)\n"
        "  --out <dir>             프레임 저장 디렉토리 (기본: frames)\n"
        "  --max-frames <N>        수신 후 N프레임에서 종료 (기본: 0=무제한)\n"
        "\n예:\n"
        "  %s --host 127.0.0.1 --port 4433 --path /camera\n",
        app, app);
}
// 커맨드 라인 옵션을 저장하는 구조체
typedef struct {
    const char* host;
    const char* addr;
    int port;
    const char* path;
    const char* alpn;
    const char* sni;
    int no_verify;
    const char* out_dir;
    int max_frames;
} cli_opts_t;

// 애플리케이션의 상태를 관리하는 컨텍스트 구조체
typedef struct {
    int max_frames;
    int frame_count;
    char out_dir[256];

    // 스트림 상태를 저장하기 위한 변수들
    uint8_t* frame_buffer;
    size_t   buffer_size;
    uint64_t frame_size;
    size_t   received_size;
} app_ctx_t;
static volatile int exit_flag = 0; 

// 클라이언트 옵션을 저장하기 위한 구조체
typedef struct {
    const char* server_name;
    int server_port;
    const char* path;
} client_options_t;

// 전역 종료 플래그
extern volatile sig_atomic_t g_running;

// main 함수보다 먼저 사용될 함수들의 프로토타입 선언
static int parse_cli(int argc, char** argv, cli_opts_t* opt);
static int ensure_dir(const char* dir);
static void handle_sigint(int signum);
static const char* picoquic_state_str_(picoquic_state_enum state);

/* ────────────────────── 클라이언트 콜백 함수 ────────────────────── */

static int client_cb(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t event, void* callback_ctx, void* v_stream_ctx)
{
    app_ctx_t* app = (app_ctx_t*)callback_ctx;

    if (event == picoquic_callback_stream_data) {
        uint8_t* p = bytes;
        uint8_t* p_max = p + length;

        while (p < p_max) {
            if (app->frame_size == 0) {
                size_t len_len = picoquic_varint_decode(p, (size_t)(p_max - p), &app->frame_size);
                if (len_len == 0) {
                    break;
                }
                p += len_len;

                if (app->frame_size > app->buffer_size) {
                    if (app->frame_buffer) free(app->frame_buffer);
                    app->frame_buffer = (uint8_t*)malloc(app->frame_size);
                    app->buffer_size = (app->frame_buffer) ? app->frame_size : 0;
                }
                app->received_size = 0;
                fprintf(stderr, "새 프레임 수신 시작 (스트림 #%" PRIu64 ", 크기: %" PRIu64 " 바이트)\n", stream_id, app->frame_size);
            }

            if (app->frame_size > 0 && app->frame_buffer) {
                size_t to_copy = (size_t)(p_max - p);
                if (app->received_size + to_copy > app->frame_size) {
                    to_copy = (size_t)app->frame_size - app->received_size;
                }

                memcpy(app->frame_buffer + app->received_size, p, to_copy);
                app->received_size += to_copy;
                p += to_copy;

                if (app->received_size == app->frame_size) {
                    app->frame_count++;
                    fprintf(stderr, "프레임 #%d 수신 완료! (%zu 바이트)\n", app->frame_count, (size_t)app->frame_size);

                    if (app->out_dir[0] != '\0') {
                        char file_path[512];
                        snprintf(file_path, sizeof(file_path), "%s/frame_%04d.jpg", app->out_dir, app->frame_count);
                        FILE* f = fopen(file_path, "wb");
                        if (f) {
                            fwrite(app->frame_buffer, 1, app->frame_size, f);
                            fclose(f);
                        }
                    }

                    app->frame_size = 0;
                    app->received_size = 0;

                    if (app->max_frames > 0 && app->frame_count >= app->max_frames) {
                        fprintf(stdout, "최대 프레임 수(%d)에 도달하여 연결을 종료합니다.\n", app->max_frames);
                        picoquic_close(cnx, 0);
                        g_running = 0;
                    }
                }
            }
        }
    } else if (event == picoquic_callback_stream_fin) {
        fprintf(stderr, "스트림 #%" PRIu64 " FIN 수신.\n", stream_id);
    } else if (event == picoquic_callback_close || event == picoquic_callback_application_close) {
        fprintf(stderr, "연결이 종료되었습니다.\n");
        if (app->frame_buffer) {
            free(app->frame_buffer);
            app->frame_buffer = NULL;
        }
        g_running = 0;
    }
    return 0;
}

/* ────────────────────── Main 함수 ────────────────────── */

int main(int argc, char** argv) {
    fprintf(stderr, "[mquic_client] build: %s %s\n", __DATE__, __TIME__);

    cli_opts_t opt;
    if (parse_cli(argc, argv, &opt) != 0) return 1;

    fprintf(stderr, "[로그] 1. 클라이언트 시작 (host=%s, port=%d, path=%s, alpn=%s)\n",
            opt.host, opt.port, opt.path, opt.alpn ? opt.alpn : "(null)");

    (void)ensure_dir(opt.out_dir);
    (void)ensure_dir("binlog");
    (void)ensure_dir("qlogs");

    signal(SIGINT, handle_sigint);

    picoquic_quic_t* quic = picoquic_create(
        1, NULL, NULL, NULL, opt.alpn, NULL, NULL,
        NULL, NULL, NULL, picoquic_current_time(), NULL, NULL, NULL, 0);
    if (!quic) { fprintf(stderr, "[client] picoquic_create() failed\n"); return 1; }

    picoquic_set_binlog(quic, "binlog");
    picoquic_set_qlog(quic,  "qlogs");
    picoquic_use_unique_log_names(quic, 1);
    picoquic_set_key_log_file_from_env(quic);
    {
        picoquic_tp_t ctp = {0};
        ctp.initial_max_data                      = 64ull * 1024 * 1024;
        ctp.initial_max_stream_data_bidi_local    = 8ull * 1024 * 1024;
        ctp.initial_max_stream_data_bidi_remote   = 8ull * 1024 * 1024;
        ctp.initial_max_stream_data_uni           = 8ull * 1024 * 1024;
        ctp.initial_max_stream_id_bidir           = 16;
        ctp.initial_max_stream_id_unidir          = 1024;

        /* 필수/권장 추가 */
        ctp.max_packet_size            = 1500;  // (>= 1200)
        ctp.ack_delay_exponent         = 3;
        ctp.max_ack_delay              = 25;    // ms
        ctp.active_connection_id_limit = 8;     // ★ 여기 핵심
    }
    if (opt.no_verify) {
        picoquic_set_verify_certificate_callback(quic, NULL, NULL);
    }

    struct sockaddr_storage peer; int is_name = 0;
    fprintf(stderr, "[로그] 2. 서버 주소 확인 중...\n");
    const char* connect_host = opt.addr ? opt.addr : opt.host;
    const char* sni_host     = opt.sni  ? opt.sni  : opt.host;
    if (picoquic_get_server_address(connect_host, opt.port, &peer, &is_name) != 0) {
        fprintf(stderr, "[client] resolve failed: %s:%d\n", opt.host, opt.port);
        picoquic_free(quic); return 1;
    }
    fprintf(stderr, "[로그] 2. 서버 주소 확인 완료.\n");

    fprintf(stderr, "[로그] 3. QUIC 연결 객체 생성 중...\n");
    picoquic_cnx_t* cnx = picoquic_create_cnx(
        quic, picoquic_null_connection_id, picoquic_null_connection_id,
        (struct sockaddr*)&peer, picoquic_current_time(),
        0, sni_host, opt.alpn, 1);
    if (!cnx) { fprintf(stderr, "[client] create_cnx failed\n"); picoquic_free(quic); return 1; }

    app_ctx_t app = {0};
    app.max_frames = opt.max_frames;
    strncpy(app.out_dir, opt.out_dir, sizeof(app.out_dir) - 1);
    picoquic_set_callback(cnx, client_cb, &app);
    fprintf(stderr, "[로그] 3. QUIC 연결 객체 생성 완료.\n");

    fprintf(stderr, "[로그] 4. QUIC 연결 시작 (핸드셰이크 시작)...\n");
    if (picoquic_start_client_cnx(cnx) != 0) {
        fprintf(stderr, "[client] start_client_cnx failed\n");
        goto cleanup;
    }

    fprintf(stderr, "[로그] 5. 핸드셰이크가 완료되기를 기다립니다...\n");
    picoquic_state_enum state = picoquic_get_cnx_state(cnx);
    uint64_t wait_start_time = picoquic_current_time();
    while (state < picoquic_state_client_ready_start &&
           state != picoquic_state_disconnecting &&
           state != picoquic_state_disconnected &&
           picoquic_current_time() - wait_start_time < 10000000)
    {
        picoquic_packet_loop_param_t lp_wait = {0};
        picoquic_packet_loop_v2(quic, &lp_wait, NULL, NULL);
        state = picoquic_get_cnx_state(cnx);
    }

    fprintf(stderr, "[로그] 6. 핸드셰이크 후 연결 상태: %s\n", picoquic_state_str_(state));
    if (state < picoquic_state_client_ready_start) {
        fprintf(stderr, "[에러] ready 상태가 아님 → CONNECT 미전송.\n");
        goto cleanup;
    }

    /* ============================ WebTransport CONNECT 요청 섹션 ============================ */
    {
        fprintf(stderr, "[로그] 7. WebTransport CONNECT 요청 생성 및 전송 시도...\n");
        char authority[256];
        snprintf(authority, sizeof(authority), "%s:%d", sni_host, opt.port);

        /* 요청용(클라이언트-초기화) bidirectional stream id */
        uint64_t ctrl_id = picoquic_get_next_local_stream_id(cnx, 0);

        /* 임시 헤더 블록(문자열). 실제로는 QPACK 인코딩 권장 */
        uint8_t hdr_block[512];
        int hdr_len = snprintf((char*)hdr_block, sizeof(hdr_block),
            ":method: CONNECT\r\n"
            ":scheme: https\r\n"
            ":authority: %s\r\n"
            ":path: %s\r\n"
            ":protocol: webtransport\r\n",
            authority, opt.path);

        if (hdr_len <= 0 || (size_t)hdr_len >= sizeof(hdr_block)) {
            fprintf(stderr, "[client] header block build failed/too long\n");
            goto cleanup;
        }

        /* ✅ 프로젝트 헤더의 시그니처에 정확히 맞춰 호출 */
        int ret = h3zero_create_headers_frame(
            cnx,               /* picoquic_cnx_t* */
            ctrl_id,           /* stream id */
            hdr_block,         /* const uint8_t* headers */
            (size_t)hdr_len,   /* size_t headers_len */
            0                  /* fin=0 */
        );
        if (ret != 0) {
            fprintf(stderr, "[client] CONNECT request failed to be sent (ret=%d)\n", ret);
            goto cleanup;
        }

        fprintf(stdout, "[client] CONNECT sent (ctrl stream=%" PRIu64 ")\n", ctrl_id);
    }
    /* ====================================================================================== */

    fprintf(stderr, "[로그] 8. 메인 데이터 패킷 루프 진입 (서버 응답 대기 중)...\n");
    while (g_running && picoquic_get_cnx_state(cnx) < picoquic_state_disconnecting) {
        picoquic_packet_loop_param_t lp = {0};
        int ret = picoquic_packet_loop_v2(quic, &lp, NULL, NULL);
        if (ret != 0) {
            fprintf(stderr, "[client] packet loop failed with ret=%d\n", ret);
            break;
        }
    }
    fprintf(stderr, "[로그] 8. 메인 데이터 패킷 루프 종료.\n");

cleanup:
    fprintf(stdout, "[client] cleaning up...\n");
    if (app.frame_buffer) {
        free(app.frame_buffer);
    }
    picoquic_free(quic);
    return 0;
}

/* ────────────────────── 유틸리티 함수 및 전역 변수 ────────────────────── */

// Ctrl+C 입력을 처리하기 위한 전역 플래그



static void handle_sigint(int signum)
{
    // 컴파일러가 최적화로 exit_flag 검사를 생략하지 않도록 volatile로 선언
    exit_flag = 1;
}

/**
 * @brief picoquic 연결 상태 enum을 문자열로 변환 (디버깅용)
 */
static const char* picoquic_state_str_(picoquic_state_enum state)
{
    switch (state) {
    case picoquic_state_client_init:            return "client_init";
    case picoquic_state_client_init_sent:       return "client_init_sent";
    case picoquic_state_client_renegotiate:     return "client_renegotiate";
    case picoquic_state_client_retry_received:  return "client_hrr_received";
    case picoquic_state_client_init_resent:     return "client_init_resent";
    case picoquic_state_client_ready_start:     return "client_ready_start";
    case picoquic_state_server_init:            return "server_init";
    case picoquic_state_ready:                  return "ready";
    case picoquic_state_disconnecting:          return "disconnecting";
    case picoquic_state_closing_received:       return "closing_received";
    case picoquic_state_closing:                return "closing";
    case picoquic_state_draining:               return "draining";
    case picoquic_state_disconnected:           return "disconnected";
    default:                                    return "unknown_state";
    }
}

/**
 * @brief 사용법을 출력하는 함수
 */

/**
 * @brief Command Line Interface(CLI) 인자들을 파싱하는 함수
 *
 * @param argc main 함수에서 받은 인자 개수
 * @param argv main 함수에서 받은 인자 배열
 * @param options 파싱 결과를 저장할 구조체 포인터
 * @return 성공 시 0, 실패 시 -1
 */
static int parse_cli(int argc, char** argv, cli_opts_t* opt)
{
    // 기본값
    memset(opt, 0, sizeof(*opt));
    opt->port       = 4433;
    opt->path       = "/";
    opt->alpn       = "h3";
    opt->sni        = NULL;
    opt->no_verify  = 1;        // 자체 인증서 환경이면 1 유지, 아니면 0
    opt->out_dir    = "frames";
    opt->max_frames = 0;        // 0 = 무제한

    static struct option long_opts[] = {
        {"host",       required_argument, 0, 'h'},
        {"addr",       required_argument, 0, 'a'},
        {"port",       required_argument, 0, 'p'},
        {"path",       required_argument, 0, 'P'},
        {"alpn",       required_argument, 0, 'A'},
        {"sni",        required_argument, 0, 's'},
        {"no-verify",  no_argument,       0, 'k'},
        {"out",        required_argument, 0, 'o'},
        {"max-frames", required_argument, 0, 'm'},
        {"help",       no_argument,       0, '?'},
        {0,0,0,0}
    };

    int c, idx;
    // getopt_long는 optind를 이동시킵니다.
    while ((c = getopt_long(argc, argv, "", long_opts, &idx)) != -1) {
        switch (c) {
        case 'h': opt->host = optarg; break;
        case 'a': opt->addr = optarg; break;
        case 'p': {
            long v = strtol(optarg, NULL, 10);
            if (v <= 0 || v > 65535) {
                fprintf(stderr, "Invalid --port: %s\n", optarg);
                return -1;
            }
            opt->port = (int)v;
            break;
        }
        case 'P': opt->path = optarg; break;
        case 'A': opt->alpn = optarg; break;
        case 's': opt->sni  = optarg; break;
        case 'k': opt->no_verify = 1;  break;
        case 'o': opt->out_dir = optarg; break;
        case 'm': {
            long v = strtol(optarg, NULL, 10);
            if (v < 0) { fprintf(stderr, "Invalid --max-frames: %s\n", optarg); return -1; }
            opt->max_frames = (int)v;
            break;
        }
        case '?':
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    // 필수값 체크
    if (!opt->host) {
        fprintf(stderr, "Error: --host 는 필수입니다.\n");
        print_usage(argv[0]);
        return -1;
    }
    if (!opt->path) opt->path = "/";

    return 0;
}
static int ensure_dir(const char* dir)
{
    if (dir == NULL || dir[0] == '\0') return 0;

    struct stat st;
    if (stat(dir, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        fprintf(stderr, "[ensure_dir] path exists but not a directory: %s\n", dir);
        return -1;
    }

    if (mkdir(dir, 0755) == 0) return 0;

    if (errno == EEXIST) return 0;
    perror("[ensure_dir] mkdir");
    return -1;
}