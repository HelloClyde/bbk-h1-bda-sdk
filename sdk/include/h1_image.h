#ifndef H1_IMAGE_H
#define H1_IMAGE_H
#include <stdint.h>
/* Bounded JPEG/PNG -> caller-owned RGB565; <=256KiB input, <=480x272 output. */
int h1_image_decode_rgb565(const unsigned char *,unsigned,uint16_t **,unsigned *,unsigned *,unsigned,unsigned);
#endif
