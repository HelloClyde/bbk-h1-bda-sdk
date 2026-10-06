#ifndef H1_GAME_H
#define H1_GAME_H
#include "h1_input.h"

/* Native V1.41 game APIs, observed in BBVM, Mission and Thunder.
 * Event identifiers and game scancodes are DIFFERENT namespaces. */
static inline unsigned h1_game_scancode(unsigned event_key)
{
    static const h1_u8 codes[43] = {0,16,17,18,19,20,21,22,30,31,32,33,34,35,36,
        104,44,45,46,47,48,49,109,57,1,28,105,108,106,23,24,25,37,38,86,
        106,50,103,111,28,105,1,42};
    return event_key >= 1 && event_key <= 42 ? codes[event_key] : 0;
}
static inline int h1_game_scancode_down(unsigned scancode)
{
    typedef int (*fn_type)(unsigned);
    fn_type fn = (fn_type)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT), 0x9D8u);
    return scancode && fn ? fn(scancode) != 0 : 0;
}
/* Shared scancodes: Enter/Confirm, left Alt/Left, right Alt/Right.
 * This query cannot distinguish those physical aliases; use queue identity. */
static inline int h1_game_key_down(unsigned event_key)
{ return h1_game_scancode_down(h1_game_scancode(event_key)); }

typedef struct { h1_u8 held[45], alias_owner[256]; } h1_game_input;
static inline void h1_game_input_reset(h1_game_input *s)
{
    unsigned i;
    for (i=0;i<45;++i) s->held[i]=0;
    for (i=0;i<256;++i) s->alias_owner[i]=0;
}
/* Feed every event without discarding touch/system events. Held query is
 * authoritative; a synthetic release must not erase a different held key. */
static inline void h1_game_input_event(h1_game_input *s, int kind, unsigned key)
{
    unsigned code=h1_game_scancode(key);
    if (!code) return;
    if (kind==H1_EVENT_KEY_DOWN) {
        s->held[key]=1;
        if (code==28 || code==105 || code==106) s->alias_owner[code]=(h1_u8)key;
    } else if (kind==H1_EVENT_KEY_UP) s->held[key]=0;
}
/* Call once per emulated frame, not on every audio wait iteration. */
static inline void h1_game_input_sample_selected(h1_game_input *s,const h1_u8 selected[45])
{
    h1_u8 sampled[256]={0}, down[256]={0};
    unsigned key;
    for (key=1;key<=42;++key) {
        unsigned code=h1_game_scancode(key);
        if (selected && !selected[key]) { s->held[key]=0;continue; }
        if (!sampled[code]) {
            down[code]=(h1_u8)h1_game_scancode_down(code); sampled[code]=1;
            if (!down[code]) s->alias_owner[code]=0;
        }
        s->held[key]=down[code];
        if (code==28 || code==105 || code==106) {
            unsigned owner=s->alias_owner[code];
            if (!owner) owner=code==28 ? H1_KEY_ENTER : code==105 ? H1_KEY_LEFT : H1_KEY_RIGHT;
            s->held[key]=(h1_u8)(down[code] && owner==key);
        }
    }
}
static inline void h1_game_input_sample(h1_game_input *s)
{ h1_game_input_sample_selected(s,0); }

typedef struct {
    volatile h1_u32 *pixels;
    h1_u16 width, height, stride_bytes, bits_per_pixel;
} h1_game_framebuffer;
static inline int h1_game_open(void)
{
    typedef int (*fn_type)(void);
    fn_type fn=(fn_type)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x84Cu);
    return fn ? fn()!=0 : 0;
}
static inline void h1_game_close(void)
{
    typedef int (*fn_type)(void);
    fn_type fn=(fn_type)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x850u);
    if (fn) (void)fn();
}
/* Query again after UI/mode changes. No hardcoded LCD address. Format is
 * xRGB8888 (0x00RRGGBB), and stride is in BYTES. No GUI+0x070 refresh needed. */
static inline int h1_game_framebuffer_get(h1_game_framebuffer *out)
{
    typedef volatile h1_u32 *(*fn_type)(h1_u16 *,void *);
    h1_u16 info[6]={0}; volatile h1_u32 *p;
    fn_type fn=(fn_type)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x8F4u);
    if (!out) return 0;
    out->pixels=0;out->width=out->height=out->stride_bytes=out->bits_per_pixel=0;
    p=fn ? fn(info,0) : 0;
    if (!p || ((h1_u32)p & 3u) || !info[1] || !info[2] || info[5]!=32 ||
        (info[3]&3u) || info[3]<(h1_u32)info[1]*4u) return 0;
    out->pixels=p;out->width=info[1];out->height=info[2];
    out->stride_bytes=info[3];out->bits_per_pixel=info[5];return 1;
}
/* Build a row in cached RAM, then issue sequential word stores to LCD.
 * Never read back uncached destination pixels for conversion or blending. */
static inline int h1_game_blit_rgb565(const h1_game_framebuffer *fb,
    const h1_u16 *src,unsigned stride_pixels,unsigned x,unsigned y,
    unsigned width,unsigned height)
{
    h1_u32 row[480];unsigned r,c;
    if (!fb || !fb->pixels || !src || fb->bits_per_pixel!=32 ||
        (fb->stride_bytes&3u) || fb->stride_bytes<(unsigned)fb->width*4 ||
        width>480 || stride_pixels<width || x>fb->width || y>fb->height ||
        width>fb->width-x || height>fb->height-y) return 0;
    for (r=0;r<height;++r) {
        volatile h1_u32 *dst=fb->pixels+(y+r)*(fb->stride_bytes/4)+x;
        for (c=0;c<width;++c) {
            h1_u32 p=src[r*stride_pixels+c];
            h1_u32 red=(p>>11)&31,green=(p>>5)&63,blue=p&31;
            row[c]=(((red<<3)|(red>>2))<<16)|(((green<<2)|(green>>4))<<8)|((blue<<3)|(blue>>2));
        }
        for (c=0;c+8<=width;c+=8) {
            dst[c]=row[c];dst[c+1]=row[c+1];dst[c+2]=row[c+2];dst[c+3]=row[c+3];
            dst[c+4]=row[c+4];dst[c+5]=row[c+5];dst[c+6]=row[c+6];dst[c+7]=row[c+7];
        }
        for (;c<width;++c) dst[c]=row[c];
    }
    return 1;
}
#endif
