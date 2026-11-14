/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#include "h1net_gzip.h"
#include <limits.h>
#include <string.h>

int h1net_gzip_step(h1net_gzip *d, const uint8_t *input, size_t *input_size,
                   uint8_t *output, size_t *output_size, int eof)
{
    size_t available=*input_size, capacity=*output_size;
    *input_size=*output_size=0;
    if(available>UINT_MAX || !capacity || capacity>UINT_MAX)return -1;
    if(d->ended){
        if(!available)return eof?1:0;
        int reset=inflateReset2(&d->stream,16+MAX_WBITS);
        if(reset!=Z_OK)return reset==Z_MEM_ERROR?-2:-1;
        d->ended=0;
    }
    if(!d->initialized){
        int init=inflateInit2(&d->stream,16+MAX_WBITS);
        if(init!=Z_OK)return init==Z_MEM_ERROR?-2:-1;
        d->initialized=1;
    }
    d->stream.next_in=(Bytef *)input;d->stream.avail_in=(uInt)available;
    d->stream.next_out=output;d->stream.avail_out=(uInt)capacity;
    int result=inflate(&d->stream,Z_NO_FLUSH);
    *input_size=available-d->stream.avail_in;
    *output_size=capacity-d->stream.avail_out;
    if(result==Z_STREAM_END)d->ended=1;
    else if(result!=Z_OK && result!=Z_BUF_ERROR)return result==Z_MEM_ERROR?-2:-1;
    if(eof && !d->stream.avail_in && d->stream.avail_out){
        if(!d->ended)return -1;
        return 1;
    }
    if(available && !*input_size && !*output_size && !d->ended)return -1;
    return 0;
}

void h1net_gzip_close(h1net_gzip *d)
{
    if(d->initialized)inflateEnd(&d->stream);
    memset(d,0,sizeof *d);
}
