// client_main.c  (drop-in replacement)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <signal.h>      // sig_atomic_t
#include <sys/stat.h>    // mkdir
#include <sys/types.h>
#include <netinet/in.h>  // AF_INET
#include <getopt.h>      // getopt_long

#include "picotls.h"             // ptls_iovec_t
#include "picoquic.h"
#include "picoquic_utils.h"
#include "picoquic_packet_loop.h"
#include "picoquic_binlog.h"
#include "qlog.h"
#include "autoqlog.h"

#include "h3zero.h"
#include "h3zero_common.h"
#include "h3zero_client.h"       // h3zero_client_create_connect_request
#include "wt_client.h"           // app_ctx_t, client_cb, free_streams, cli_opts_t, loop_hook, ensure_dir
#include "logger.h"

/* ────────────────────── Build banner ────────────────────── */
#define BUILD_BANNER "[mquic_client] build " __DATE__ " " __TIME__

/* ────────────────────── Signal ────────────────────── */
static void handle_sigint(int sig) { (void)sig; g_running = 0; fprintf(stderr, "\n[client] SIGINT → graceful shutdown...\n"); }

/* ────────────────────── CLI ────────────────────── */
static void usage(const char* argv0) {
    fprintf(stderr,
        "Usage: %s --host <name|ip> --port <4433> --path </camera> [options]\n"
        "Options:\n"
        "  --alpn <h3|hq>       ALPN (default: h3)\n"
        "  --addr <ip-or-host>  Connect addr (override DNS); default: --host\n"
        "  --sni  <name>        TLS SNI (override); default: --host\n"
        "  --out  <dir>         Output dir (default: ./frames)\n"
        "  --frames <N>         Stop after N frames (0 = unlimited)\n"
        "  --no-verify          Disable TLS cert verification (DEV ONLY)\n"
        "  -h, --help           Show this help\n",
        argv0);
}

static int parse_cli(int argc, char** argv, cli_opts_t* o) {
    memset(o, 0, sizeof(*o));
    o->alpn = "h3";
    o->out_dir = "./frames";
    o->max_frames = 0;

    static struct option long_opts[] = {
        {"host",      required_argument, 0,  1 },
        {"port",      required_argument, 0,  2 },
        {"path",      required_argument, 0,  3 },
        {"alpn",      required_argument, 0,  4 },
        {"addr",      required_argument, 0,  5 },
        {"sni",       required_argument, 0,  6 },
        {"out",       required_argument, 0,  7 },
        {"frames",    required_argument, 0,  8 },
        {"no-verify", no_argument,       0,  9 },
        {"help",      no_argument,       0, 'h'},
        {0,0,0,0}
    };

    int optidx = 0;
    for (;;) {
        int c = getopt_long(argc, argv, "h", long_opts, &optidx);
        if (c == -1) break;
        switch (c) {
        case 1: o->host = optarg; break;
        case 2: o->port = atoi(optarg); break;
        case 3: o->path = optarg; break;
        case 4: o->alpn = optarg; break;
        case 5: o->addr = optarg; break;
        case 6: o->sni  = optarg; break;
        case 7: o->out_dir = optarg; break;
        case 8: o->max_frames = atoi(optarg); break;
        case 9: o->no_verify = 1; break;
        case 'h': usage(argv[0]); return 1;
        default: usage(argv[0]); return 1;
        }
    }

    /* fallback: 옛 포지셔널 형식 지원 (server port [path]) */
    if (!o->host && optind < argc) o->host = argv[optind++];
    if (!o->port && optind < argc) o->port = atoi(argv[optind++]);
    if (!o->path && optind < argc) o->path = argv[optind++];

    if (!o->host || !o->port || !o->path) { usage(argv[0]); return 1; }
    return 0;
}

/* ────────────────────── State string (편의) ────────────────────── */
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

/* trust-all (DEV ONLY, 외부 구현이 있으면 링크됨) */
extern ptls_verify_certificate_t PTLS_TRUST_ALL;

/* ────────────────────── Handshake loop callback ────────────────────── */
static int client_loop_cb(picoquic_quic_t* quic,
                          picoquic_packet_loop_cb_enum cb_mode,
                          void* callback_ctx,
                          void* callback_arg)
{
    (void)callback_ctx; (void)callback_arg;

    if (cb_mode == picoquic_packet_loop_ready) {
        fprintf(stderr, "[loop_cb] Waiting for packets.\n");
    }

    picoquic_cnx_t* c = picoquic_get_first_cnx(quic);
    if (c) {
        picoquic_state_enum st = picoquic_get_cnx_state(c);

        if (st == picoquic_state_client_ready_start || st == picoquic_state_ready) {
            fprintf(stderr, "[loop_cb] handshake ready -> exit loop\n");
            return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        }
        if (st == picoquic_state_disconnecting ||
            st == picoquic_state_closing ||
            st == picoquic_state_closing_received ||
            st == picoquic_state_draining ||
            st == picoquic_state_disconnected) {
            fprintf(stderr, "[loop_cb] connection closing (%d) -> exit loop\n", (int)st);
            return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        }
    }
    return 0; // 계속 대기
}

/* ────────────────────── Main ────────────────────── */
int main(int argc, char** argv) {
    fprintf(stderr, BUILD_BANNER "\n");

    cli_opts_t opt;
    if (parse_cli(argc, argv, &opt) != 0) return 1;

    fprintf(stderr, "[로그] 1. 클라이언트 시작 (host=%s, port=%d, path=%s, alpn=%s)\n",
            opt.host, opt.port, opt.path, opt.alpn ? opt.alpn : "(null)");

    /* 출력/로그 디렉토리 준비 */
    if (ensure_dir(opt.out_dir) != 0) {
        fprintf(stderr, "[client] cannot create output dir: %s\n", opt.out_dir);
        return 1;
    }
    (void)ensure_dir("binlog");
    (void)ensure_dir("qlogs");

    /* SIGINT 핸들 */
    signal(SIGINT, handle_sigint);

    /* QUIC 인스턴스 */
    picoquic_quic_t* quic = picoquic_create(
        16, NULL, NULL, NULL, opt.alpn, NULL, NULL,
        NULL, NULL, NULL, picoquic_current_time(), NULL, NULL, NULL, 0);
    if (!quic) { fprintf(stderr, "[client] picoquic_create() failed\n"); return 1; }

    /* 로깅 */
    picoquic_set_binlog(quic, "binlog");
    picoquic_set_qlog(quic,  "qlogs");
    picoquic_use_unique_log_names(quic, 1);
    picoquic_set_key_log_file_from_env(quic);

    /* 전송 파라미터 (보수적으로 큼) */
    {
        picoquic_tp_t ctp; memset(&ctp, 0, sizeof(ctp));
        ctp.initial_max_data                    = 64ull * 1024 * 1024;
        ctp.initial_max_stream_data_bidi_local  = 8ull * 1024 * 1024;
        ctp.initial_max_stream_data_bidi_remote = 8ull * 1024 * 1024;
        ctp.initial_max_stream_data_uni         = 8ull * 1024 * 1024;
        ctp.initial_max_stream_id_bidir         = 16;
        ctp.initial_max_stream_id_unidir        = 1024;
        picoquic_set_default_tp(quic, &ctp);
    }

    if (opt.no_verify) {
        /* 외부에서 PTLS_TRUST_ALL 제공된다는 가정(DEV ONLY) */
        picoquic_set_verify_certificate_callback(quic, &PTLS_TRUST_ALL, NULL);
        fprintf(stderr, "[client] WARNING: --no-verify enabled (DEV ONLY)\n");
    }

    /* 서버 주소 확인 */
    struct sockaddr_storage peer; int is_name = 0;
    fprintf(stderr, "[로그] 2. 서버 주소 확인 중...\n");
    const char* connect_host = opt.addr ? opt.addr : opt.host;
    const char* sni_host     = opt.sni  ? opt.sni  : opt.host;
    if (picoquic_get_server_address(connect_host, opt.port, &peer, &is_name) != 0) {
        fprintf(stderr, "[client] resolve failed: %s:%d\n", opt.host, opt.port);
        picoquic_free(quic); return 1;
    }
    fprintf(stderr, "[peer] family=%s\n",
        (((struct sockaddr*)&peer)->sa_family==AF_INET6)?"IPv6":
        (((struct sockaddr*)&peer)->sa_family==AF_INET) ?"IPv4":"?");
    fprintf(stderr, "[로그] 2. 서버 주소 확인 완료.\n");

    /* 연결 생성: SNI=host (H3 필수) */
    fprintf(stderr, "[로그] 3. QUIC 연결 객체 생성 중...\n");
    picoquic_cnx_t* cnx = picoquic_create_cnx(
        quic, picoquic_null_connection_id, picoquic_null_connection_id,
        (struct sockaddr*)&peer, picoquic_current_time(),
        /* proposed_version */ 0, /* SNI */ sni_host, /* ALPN */ opt.alpn, /* client */ 1);
    if (!cnx) { fprintf(stderr, "[client] create_cnx failed\n"); picoquic_free(quic); return 1; }
    fprintf(stderr, "[로그] 3. QUIC 연결 객체 생성 완료.\n");

    /* QUIC v1 강제(Version Negotiation 회피에 유리) */
    picoquic_set_desired_version(cnx, 0x00000001);

    /* 진단/상호운용 플래그 */
    cnx->grease_transport_parameters          = 1;
    cnx->local_parameters.enable_time_stamp   = 3;
    cnx->local_parameters.do_grease_quic_bit  = 1;

    /* PMTUD 보수적 */
    picoquic_cnx_set_pmtud_policy(cnx, picoquic_pmtud_delayed);
    picoquic_set_default_pmtud_policy(quic, picoquic_pmtud_delayed);

    /* H3 경로/콜백 준비 (우리 client_cb 사용) */
    picohttp_server_path_item_t paths[] = {
        { (uint8_t*)opt.path, (uint32_t)strlen(opt.path), client_cb, NULL }
    };
    picohttp_server_parameters_t sp = { .path_table = paths, .path_table_nb = 1 };

    app_ctx_t app; memset(&app, 0, sizeof(app));
    snprintf(app.out_dir, sizeof(app.out_dir), "%s", opt.out_dir);
    app.max_frames = opt.max_frames;
    paths[0].path_app_ctx = &app;

    h3zero_callback_ctx_t* h3ctx = h3zero_callback_create_context(&sp);
    if (!h3ctx) { fprintf(stderr, "[client] h3zero ctx failed\n"); picoquic_free(quic); return 1; }
    picoquic_set_callback(cnx, h3zero_callback, h3ctx);

    /* 핸드셰이크 시작 */
    fprintf(stderr, "[로그] 4. QUIC 연결 시작 (핸드셰이크 시작)...\n");
    if (picoquic_start_client_cnx(cnx) != 0) {
        fprintf(stderr, "[client] start_client_cnx failed\n");
        h3zero_callback_delete_context(cnx, h3ctx); picoquic_free(quic); return 1;
    }

    /* 패킷 루프(v1): 핸드셰이크만 */
    fprintf(stderr, "[로그] 5. 핸드셰이크 패킷 루프 진입...\n");
    {
        int sockbuf     = 1 << 20; // ~1MB
        int disable_gso = 1;       // GSO 비활성화
        if (picoquic_packet_loop(
                quic,            // 1) picoquic_quic_t*
                0,               // 2) local_port (0 = ephemeral)
                0,               // 3) local_af (0 = UNSPEC)
                0,               // 4) do_not_log (0 = 로그 허용)
                sockbuf,         // 5) socket_buffer_size
                disable_gso,     // 6) do_not_use_gso
                client_loop_cb,  // 7) 콜백 함수 포인터
                &app             // 8) 콜백 컨텍스트
            ) != 0)
        {
            fprintf(stderr, "루프로직 진입 실패\n");
            picoquic_state_enum st = picoquic_get_cnx_state(cnx);
            uint64_t app_err = picoquic_get_application_error(cnx);
            uint64_t tr_err  = picoquic_get_remote_error(cnx);
            fprintf(stderr, "[client] first loop failed: state=%d app_err=%" PRIu64 " transport_err=%" PRIu64 "\n",
                    st, app_err, tr_err);
            goto cleanup;
        }
    }
    fprintf(stderr, "[로그] 5. 핸드셰이크 패킷 루프 종료.\n");

    /* 상태 확인 */
    {
        picoquic_state_enum post = picoquic_get_cnx_state(cnx);
        fprintf(stderr, "[로그] 6. 핸드셰이크 후 연결 상태: %d (%s)\n", post, picoquic_state_str_(post));
        if (post != picoquic_state_ready && post != picoquic_state_client_ready_start) {
            fprintf(stderr, "[로그] 6. 경고: 연결이 'ready'가 아님. 계속 진행은 시도합니다.\n");
        }
    }

    /* CONNECT :protocol=webtransport (bidi stream, FIN은 닫지 않음) */
    {
        fprintf(stderr, "[로그] 7. WebTransport CONNECT 요청 생성 및 전송 시도...\n");
        char authority[256]; snprintf(authority, sizeof(authority), "%s:%d", (opt.sni?opt.sni:opt.host), opt.port);
        uint64_t ctrl_id = picoquic_get_next_local_stream_id(cnx, /*is_unidir=*/0); // bidi

        /* 스케줄러에 올리기 */
        picoquic_mark_active_stream(cnx, ctrl_id, 1, NULL);

        /* FIN=0 : 세션 유지 */
        if (h3zero_client_create_connect_request(cnx, ctrl_id, authority, opt.path, /*fin=*/0) != 0) {
            fprintf(stderr, "[client] CONNECT request failed\n");
            goto cleanup;
        }
        fprintf(stdout, "[client] CONNECT sent (ctrl stream=%" PRIu64 ")\n", ctrl_id);
    }

    /* 메인 루프 (데이터 교환/종료까지) */
    fprintf(stderr, "[로그] 8. 메인 데이터 패킷 루프 진입...\n");
    {
        picoquic_packet_loop_param_t lp; memset(&lp, 0, sizeof(lp));
        lp.local_port = 0;               // 에페메럴 포트
        lp.local_af   = 0;               // 자동(UNSPEC) → v4/v6 허용
        lp.do_not_use_gso = 1;           // GSO 비활성화
        lp.socket_buffer_size = 1<<20;   // ~1MB
        (void)picoquic_packet_loop_v2(quic, &lp, loop_hook, &app);
    }
    fprintf(stderr, "[로그] 8. 메인 데이터 패킷 루프 종료.\n");

    {
        picoquic_state_enum st = picoquic_get_cnx_state(cnx);
        uint64_t app_err  = picoquic_get_application_error(cnx);
        uint64_t tr_err   = picoquic_get_remote_error(cnx);
        fprintf(stderr, "[client] final state=%d app_err=%" PRIu64 " transport_err=%" PRIu64 "\n",
                st, app_err, tr_err);
    }

cleanup:
    fprintf(stdout, "[client] cleaning up...\n");
    if (app.streams) free_streams(&app);
    if (h3ctx) h3zero_callback_delete_context(cnx, h3ctx);
    picoquic_free(quic);
    return 0;
}
