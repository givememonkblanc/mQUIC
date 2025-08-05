// src/h3zero/h3zero_qpack.c
#include "h3zero_qpack.h"
#include "picoquic.h"    // for picoquic_varint_encode
#include <string.h>

uint8_t* h3zero_qpack_enc_header(uint8_t* out, uint8_t* out_max,
                                 const char* name, const char* value)
{
    size_t name_len  = strlen(name);
    size_t value_len = strlen(value);

    // 필요한 최소 용량 계산: 1바이트 리터럴 플래그 +
    // name_len varint + name + value_len varint + value
    size_t needed = 1
                  + picoquic_varint_encode(name_len, NULL)
                  + name_len
                  + picoquic_varint_encode(value_len, NULL)
                  + value_len;
    if ((size_t)(out_max - out) < needed) {
        return NULL;  // 버퍼 부족
    }

    // 1) Literal, without indexing, no name reference
    *out++ = 0x00;

    // 2) name length + name bytes
    size_t n = picoquic_varint_encode(name_len, out);
    out += n;
    memcpy(out, name, name_len);
    out += name_len;

    // 3) value length + value bytes
    n = picoquic_varint_encode(value_len, out);
    out += n;
    memcpy(out, value, value_len);
    out += value_len;

    return out;
}
