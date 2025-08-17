#include "h3zero_common.h"
#include <string.h>

int h3zero_server_parse_path(h3zero_callback_ctx_t* h3_ctx,
                             picoquic_cnx_t* cnx,
                             uint8_t* path, size_t path_length,
                             picohttp_call_back_event_t event,
                             struct st_h3zero_stream_ctx_t* stream_ctx)
{
    if (!h3_ctx || !path || !h3_ctx->path_table) return -1;

    for (size_t i = 0; i < h3_ctx->path_table_nb; i++) {
        picohttp_server_path_item_t* item = &h3_ctx->path_table[i];
        if (path_length >= item->path_length &&
            memcmp(path, item->path, item->path_length) == 0)
        {
            return item->path_callback(
                cnx, path, path_length,
                event,                     // GET/CONNECT 등 그대로 전달
                stream_ctx,
                item->path_app_ctx
            );
        }
    }
    return -1;
}
