/*
 * Copyright (c) 2021-2022, Private Octopus, Inc.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef H3ZERO_PROTOCOL_H
#define H3ZERO_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Common definitions for H3ZERO protocol messages
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HTTP/3 defines a number of pseudo-headers, which are fields that
 * are treated as headers but that might not be serialized as such.
 * These pseudo-headers all begin with the ':' character.
 */
#define H3ZERO_HEADER_METHOD ":method"
#define H3ZERO_HEADER_SCHEME ":scheme"
#define H3ZERO_HEADER_AUTHORITY ":authority"
#define H3ZERO_HEADER_PATH ":path"
#define H3ZERO_HEADER_PROTOCOL ":protocol"
#define H3ZERO_HEADER_STATUS ":status"

/* Common method values */
#define H3ZERO_METHOD_GET "GET"
#define H3ZERO_METHOD_POST "POST"
#define H3ZERO_METHOD_CONNECT "CONNECT"

/* Common scheme values */
#define H3ZERO_SCHEME_HTTPS "https"

/* Common status values */
#define H3ZERO_STATUS_200 "200"
#define H3ZERO_STATUS_404 "404"

/* Common content types */
#define H3ZERO_HEADER_CONTENT_TYPE "content-type"
#define H3ZERO_CONTENT_TYPE_TEXT "text/plain"
#define H3ZERO_CONTENT_TYPE_HTML "text/html"
#define H3ZERO_CONTENT_TYPE_BIN "application/octet-stream"


/* The structure h3zero_header_t is used to pass lists of headers
 * to and from the h3zero callbacks.
 */
typedef struct st_h3zero_header_t {
    const uint8_t* name;
    size_t name_len;
    const uint8_t* value;
    size_t value_len;
} h3zero_header_t;


#ifdef __cplusplus
}
#endif

#endif /* H3ZERO_PROTOCOL_H */
