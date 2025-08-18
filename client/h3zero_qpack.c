// client/h3zero_qpack.c
#include "h3zero_qpack.h"
#include "picoquic_internal.h"   // varint encode는 내부 헤더에 있음
#include <string.h>
#include <stdint.h>
#include <stddef.h>

static inline size_t quic_varint_len(uint64_t v) {
    return (v < (1ULL<<6))  ? 1 :
           (v < (1ULL<<14)) ? 2 :
           (v < (1ULL<<30)) ? 4 : 8;
}

static inline int write_varint(uint8_t **p, uint8_t *end, uint64_t v) {
    if (*p >= end) return -1;
    size_t w = picoquic_varint_encode(*p, (size_t)(end - *p), v);
    if (w == 0) return -1;
    *p += w;
    return 0;
}

uint8_t* h3zero_qpack_enc_header(uint8_t* out, uint8_t* out_max,
                                 const char* name, const char* value)
{
    size_t name_len  = strlen(name);
    size_t value_len = strlen(value);

    // 필요한 용량: 1(리터럴 플래그) + varint(name_len) + name + varint(value_len) + value
    size_t needed = 1
                  + quic_varint_len(name_len)
                  + name_len
                  + quic_varint_len(value_len)
                  + value_len;
    if ((size_t)(out_max - out) < needed) {
        return NULL;  // 버퍼 부족
    }

    uint8_t *p   = out;
    uint8_t *end = out_max;

    // 1) Literal, without indexing, no name reference
    *p++ = 0x00;

    // 2) name length + name bytes
    if (write_varint(&p, end, (uint64_t)name_len) != 0) return NULL;
    memcpy(p, name, name_len);
    p += name_len;

    // 3) value length + value bytes
    if (write_varint(&p, end, (uint64_t)value_len) != 0) return NULL;
    memcpy(p, value, value_len);
    p += value_len;

    return p;
}
