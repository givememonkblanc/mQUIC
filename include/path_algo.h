#pragma once
#include <stdint.h>
#include <stddef.h>
#include "picoquic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct path_algo_s path_algo_t;

/* 생성/파괴 */
path_algo_t* path_algo_create(picoquic_cnx_t* cnx);
void         path_algo_destroy(path_algo_t* a);

/* 주기 갱신(경로 수/상태 반영) */
void         path_algo_refresh(path_algo_t* a, picoquic_cnx_t* cnx);

/* 이번 프레임을 실을 경로 선택(인덱스 반환, 실패 시 0 또는 -1) */
int          path_algo_pick_send_path(path_algo_t* a, picoquic_cnx_t* cnx);

/* 주기적 프로빙할 경로 선택(없으면 -1) */
int          path_algo_pick_probe_path(path_algo_t* a, picoquic_cnx_t* cnx, uint64_t now_us);

/* 전송/유휴 이벤트 힌트 */
void         path_algo_on_sent(path_algo_t* a, int path_idx, size_t bytes);
void         path_algo_on_idle(path_algo_t* a, uint64_t now_us);

#ifdef __cplusplus
}
#endif
