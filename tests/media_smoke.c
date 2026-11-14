#include "h1_sdk.h"
#include "h1_draw.h"
#include "h1_font16.h"
#include "h1_image.h"
#include "h1_aac.h"
#include "h1_media_runtime.h"
#include <stdlib.h>
uint32_t h1ns_time_ms(void){return 0;}
void h1ns_panic(int error){(void)error;for(;;){}}
static uint16_t pixels[480*272];
int h1_app_main(void)
{
    h1_canvas c={pixels,480,272,480,0,272,&h1_font16_cjk};
    uint16_t *image=0;unsigned w=0,h=0;h1_aac decoder;
    h1_aac_init(&decoder,0,0);
    h1_draw_rect(&c,0,0,480,272,0);
    h1_draw_text(&c,"H1 / 日本語 / 한국어",4,4,476,0xffff);
    if(!h1_image_decode_rgb565((const unsigned char *)"invalid image",13,&image,&w,&h,32,32))free(image);
    h1ns_heap_release();return h1_aac_open(&decoder,0,0);
}
