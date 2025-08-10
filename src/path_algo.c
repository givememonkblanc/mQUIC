#include "path_algo.h"
#include <stdlib.h>
#include "picoquic_internal.h"  /* <-- 추가! 내부 구조체/필드 접근 가능 */
#include <string.h>

struct path_algo_s {
    int       last_send;       /* 마지막으로 보낸 경로 인덱스 */
    uint64_t  next_probe_us;   /* 다음 프로빙 시각 */
};

static int is_path_sendable(picoquic_path_t* p)
{
    if (!p || !p->first_tuple) return 0;
    /* 경로 검증 완료 & 사용 가능 */
    if (!p->first_tuple->challenge_verified) return 0;
    if (p->path_is_demoted) return 0;
    return 1;
}

path_algo_t* path_algo_create(picoquic_cnx_t* cnx)
{
    (void)cnx;
    path_algo_t* a = (path_algo_t*)calloc(1, sizeof(path_algo_t));
    if (!a) return NULL;
    a->last_send = -1;
    a->next_probe_us = 0;
    return a;
}

void path_algo_destroy(path_algo_t* a)
{
    if (a) free(a);
}

void path_algo_refresh(path_algo_t* a, picoquic_cnx_t* cnx)
{
    (void)cnx;
    if (!a) return;
    /* 필요 시 상태 갱신 자리. 현재는 보관 값 그대로 사용 */
}

/* RTT 최소 경로 우선, 없으면 첫 번째 sendable 경로, 그래도 없으면 0 */
int path_algo_pick_send_path(path_algo_t* a, picoquic_cnx_t* cnx)
{
    if (!a || !cnx || cnx->nb_paths <= 0) return -1;

    int best = -1;
    uint64_t best_rtt = (uint64_t)-1;

    for (int i = 0; i < cnx->nb_paths; i++) {
        picoquic_path_t* p = cnx->path[i];
        if (!is_path_sendable(p)) continue;

        uint64_t rtt = p->rtt_min; /* 또는 p->smoothed_rtt */
        if (rtt == 0) rtt = p->smoothed_rtt;
        if (rtt == 0) rtt = 1;

        if (rtt < best_rtt) {
            best_rtt = rtt;
            best = i;
        }
    }

    if (best < 0) {
        /* 모든 경로가 미검증이면 0으로 보정 */
        best = 0;
    }
    a->last_send = best;
    return best;
}

/* 250ms 간격으로 “다른” 경로를 가볍게 깨우기 */
int path_algo_pick_probe_path(path_algo_t* a, picoquic_cnx_t* cnx, uint64_t now_us)
{
    if (!a || !cnx || cnx->nb_paths <= 1) return -1;
    if (now_us < a->next_probe_us) return -1;

    int start = (a->last_send >= 0) ? (a->last_send + 1) % cnx->nb_paths : 0;
    for (int k = 0; k < cnx->nb_paths; k++) {
        int i = (start + k) % cnx->nb_paths;
        if (i == a->last_send) continue;
        picoquic_path_t* p = cnx->path[i];
        if (!p) continue;
        /* 검증 상태와 무관하게 간헐적 0바이트를 흘려 깨우기/유지 */
        a->next_probe_us = now_us + 250000; /* 250ms */
        return i;
    }
    return -1;
}

void path_algo_on_sent(path_algo_t* a, int path_idx, size_t bytes)
{
    (void)bytes;
    if (!a) return;
    a->last_send = path_idx;
}

void path_algo_on_idle(path_algo_t* a, uint64_t now_us)
{
    (void)now_us;
    /* 필요 시 idle 기반 정책 훅 */
}
