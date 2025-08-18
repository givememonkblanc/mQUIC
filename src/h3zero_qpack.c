// src/h3zero/h3zero_qpack.c
#include "h3zero_qpack.h"
#include <string.h>
#include <stdint.h>

/* HPACK/QPACK prefix-integer encoding (RFC7541 §5.1, RFC9204 §4.1.1) */
static inline uint8_t* qpack_write_pref_int(uint8_t* p, uint8_t* end,
                                            uint64_t value,
                                            unsigned prefix_bits,
                                            uint8_t first_byte_prefix)
{
    const uint64_t prefix_max = ((1u << prefix_bits) - 1u);
    if (p >= end) return NULL;

    if (value < prefix_max) {
        *p++ = (uint8_t)(first_byte_prefix | (uint8_t)value);
        return p;
    }

    *p++ = (uint8_t)(first_byte_prefix | (uint8_t)prefix_max);
    value -= prefix_max;

    while (value >= 128) {
        if (p >= end) return NULL;
        *p++ = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
    }
    if (p >= end) return NULL;
    *p++ = (uint8_t)value;
    return p;
}

/* 8-bit prefix string literal: H(1) + len(7+) + bytes (RFC9204 §4.1.2) */
static inline uint8_t* qpack_write_str8(uint8_t* p, uint8_t* end,
                                        const uint8_t* s, size_t len,
                                        int huffman /*0 or 1*/)
{
    if (p >= end) return NULL;
    uint8_t first = huffman ? 0x80 : 0x00;         /* H bit in MSB */
    p = qpack_write_pref_int(p, end, (uint64_t)len, 7, first);
    if (!p) return NULL;
    if ((size_t)(end - p) < len) return NULL;
    memcpy(p, s, len);
    return p + len;
}

/*
 * Literal Field Line with Literal Name (RFC9204 §4.5.6)
 * First byte: 0b001 N H | NameLen(3+)
 *  - N(never-indexed)는 보수적으로 1로 설정(원하면 0으로 바꿔도 됨)
 *  - H(이름 Huffman)는 0 (평문)
 * value는 8-bit prefix string literal (H=0)로 기록
 */
uint8_t* h3zero_qpack_enc_header(uint8_t* out, uint8_t* out_max,
                                 const char* name, const char* value)
{
    if (!out || !out_max || !name || !value) return NULL;

    const uint8_t* n = (const uint8_t*)name;
    const uint8_t* v = (const uint8_t*)value;
    const size_t nlen = strlen(name);
    const size_t vlen = strlen(value);

    /* 001 N H -----  (상위 5비트) */
    const int N = 1;        /* never-indexed 추천: 1 */
    const int Hn = 0;       /* name huffman: 0 (평문) */
    const uint8_t first_prefix = (uint8_t)((0b001 << 5) | (N << 4) | (Hn << 3)); // 0x20 | N<<4 | Hn<<3

    /* NameLen(3+) 을 같은 첫 바이트에 prefix-integer로 결합 */
    uint8_t* p = qpack_write_pref_int(out, out_max, (uint64_t)nlen, 3, first_prefix);
    if (!p) return NULL;

    /* name bytes */
    if ((size_t)(out_max - p) < nlen) return NULL;
    memcpy(p, n, nlen);
    p += nlen;

    /* value: H=0 + len(7+) + bytes */
    p = qpack_write_str8(p, out_max, v, vlen, /*H=*/0);
    if (!p) return NULL;

    return p;
}
