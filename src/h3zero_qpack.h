// src/h3zero/h3zero_qpack.h
#ifndef H3ZERO_QPACK_H
#define H3ZERO_QPACK_H
#include <stdint.h>

uint8_t* h3zero_qpack_enc_header(uint8_t* out, uint8_t* out_max,
                                 const char* name, const char* value);

#endif // H3ZERO_QPACK_H
