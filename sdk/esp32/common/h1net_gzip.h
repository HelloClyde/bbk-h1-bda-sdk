/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#ifndef H1NET_GZIP_H
#define H1NET_GZIP_H

#include <stddef.h>
#include <stdint.h>
#include "zlib.h"

/* A gzip-only decoder: validates headers, CRC32 and ISIZE, including members.
 * The caller owns bounded input/output buffers and enforces decoded byte limits.
 * Zero-initialize before first use; always call close, including on failure. */
typedef struct {
    z_stream stream;
    int initialized, ended;
} h1net_gzip;

/* *input: available on entry, consumed on return. *output: capacity -> produced.
 * eof means no more encoded bytes can arrive. Return 1 for verified final EOF,
 * 0 for progress/need input or output, -1 for malformed/truncated gzip,
 * -2 for allocation failure. Pending input must be passed again by the caller. */
int h1net_gzip_step(h1net_gzip *decoder, const uint8_t *input, size_t *input_size,
                   uint8_t *output, size_t *output_size, int eof);
void h1net_gzip_close(h1net_gzip *decoder);

#endif
