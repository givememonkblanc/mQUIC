#include "h3zero_common.h"
#include <string.h>

int h3zero_server_parse_path(h3zero_callback_ctx_t* h3_ctx,
                             picoquic_cnx_t* cnx,
                             uint8_t* path,
                             size_t path_length)
{
    if (h3_ctx == NULL || path == NULL || h3_ctx->path_table == NULL) {
        return -1;
    }

    for (size_t i = 0; i < h3_ctx->path_table_nb; i++) {
        picohttp_server_path_item_t* item = &h3_ctx->path_table[i];
        if (path_length >= item->path_length &&
            memcmp(path, item->path, item->path_length) == 0)
        {
            return item->path_callback(
                cnx,
                path,
                path_length,
                picohttp_callback_get,
                NULL, /* stream_ctx */
                NULL  /* app_ctx: 최신 구조에서는 제거됨 */
            );
        }
    }

    return -1; /* No match found */
}
