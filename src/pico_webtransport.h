/*
* Author: Christian Huitema
* Copyright (c) 2023, Private Octopus, Inc.
* All rights reserved.
*/
#ifndef pico_webtransport_H
#define pico_webtransport_H

#include "h3zero_common.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capsule types defined for web transport */
#define picowt_capsule_close_webtransport_session 0x2843
#define picowt_capsule_drain_webtransport_session 0x78ae

/* Set required transport parameters for web transport  */
void picowt_set_transport_parameters(picoquic_cnx_t* cnx);

/* Create the control stream for the Web Transport session on the client. */
h3zero_stream_ctx_t* picowt_set_control_stream(picoquic_cnx_t* cnx, h3zero_callback_ctx_t* h3_ctx);

/*
* picowt_prepare_client_cnx: prepare connection & contexts (client)
*/
int picowt_prepare_client_cnx(picoquic_quic_t* quic, struct sockaddr* server_address,
    picoquic_cnx_t** p_cnx, h3zero_callback_ctx_t** p_h3_ctx,
    h3zero_stream_ctx_t** p_stream_ctx,
    uint64_t current_time, const char* sni);

/* WebTransport CONNECT (server replies 200) */
int picowt_connect(picoquic_cnx_t* cnx, h3zero_callback_ctx_t* ctx,
    h3zero_stream_ctx_t* stream_ctx, const char* authority, const char* path,
    picohttp_post_data_cb_fn wt_callback, void* wt_ctx);

/* CLOSE / DRAIN capsules */
int picowt_send_close_session_message(picoquic_cnx_t* cnx,
    h3zero_stream_ctx_t* control_stream_ctx, uint32_t picowt_err, const char* err_msg);

int picowt_send_drain_session_message(picoquic_cnx_t* cnx,
    h3zero_stream_ctx_t* control_stream_ctx);

/* capsule container */
typedef struct st_picowt_capsule_t {
    h3zero_capsule_t h3_capsule;
    uint32_t         error_code;
    const uint8_t*   error_msg;
    size_t           error_msg_len;
} picowt_capsule_t;

int  picowt_receive_capsule(picoquic_cnx_t* cnx, h3zero_stream_ctx_t* stream_ctx,
     const uint8_t* bytes, const uint8_t* bytes_max, picowt_capsule_t* capsule,
     h3zero_callback_ctx_t* h3_ctx);

void picowt_release_capsule(picowt_capsule_t* capsule);

/* deregister (no-op) */
void picowt_deregister(picoquic_cnx_t* cnx, h3zero_callback_ctx_t* h3_ctx,
    h3zero_stream_ctx_t* control_stream_ctx);

/* Create local WT stream; send unidirectional header if needed */
h3zero_stream_ctx_t* picowt_create_local_stream(picoquic_cnx_t* cnx, int is_bidir,
    h3zero_callback_ctx_t* h3_ctx, uint64_t control_stream_id);

#ifdef __cplusplus
}
#endif
#endif /* pico_webtransport_H */
