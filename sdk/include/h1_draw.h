#ifndef H1_DRAW_H
#define H1_DRAW_H
#include <stdint.h>
typedef struct {unsigned count;const uint16_t *codepoints;const uint8_t *widths;const uint16_t (*rows)[16];} h1_font16;
/* Positive width/height, stride in pixels >=width; caller owns RGB565 storage.
 * Sorted BMP font codepoints, 8/16px widths. U+25A1 is optional fallback.
 * Vertical clip bounds are half-open; geometry is in screen pixels. */
typedef struct {uint16_t *pixels;int width,height,stride,clip_top,clip_bottom;const h1_font16 *font;} h1_canvas;
int h1_utf8_next(const char **);
unsigned h1_draw_advance(const h1_font16 *,unsigned);
void h1_draw_rect(h1_canvas *,int,int,int,int,uint16_t);
void h1_draw_glyph(h1_canvas *,unsigned,int,int,int,uint16_t);
const char *h1_draw_text_at(h1_canvas *,const char *,int *,int,int,uint16_t);
const char *h1_draw_text(h1_canvas *,const char *,int,int,int,uint16_t);
void h1_draw_ellipsis(h1_canvas *,const char *,int,int,int,uint16_t);
void h1_draw_round(h1_canvas *,int,int,int,int,int,uint16_t);
void h1_draw_dot(h1_canvas *,int,int,int,uint16_t);
void h1_draw_line(h1_canvas *,int,int,int,int,uint16_t);
int h1_draw_width(const h1_font16 *,const char *);
#endif
