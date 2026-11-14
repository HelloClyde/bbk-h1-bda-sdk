/* Fixed-cell native pixel shapes; direct clipped row writes, no resampling. */
#ifndef BILI_FONT_COUNT
#include "font.h"
#endif
static uint16_t pixels[480*272];
static int clip_top,clip_bottom=272;
static int bu_utf8_string(const char **s)
{
    const uint8_t *p=(const uint8_t *)*s;unsigned b=*p,n=b<128?1:b>=194&&b<=223?2:b>=224&&b<=239?3:b>=240&&b<=244?4:0;
    if(!b)return -1;if(!n){++*s;return 0xfffd;}
    unsigned cp=n==1?b:b&((1u<<(7-n))-1u);
    for(unsigned i=1;i<n;++i){if(!p[i]||(p[i]&192)!=128){++*s;return 0xfffd;}cp=(cp<<6)|(p[i]&63);}
    *s+=n;return (n==2&&cp<128)||(n==3&&cp<2048)||(n==4&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)?0xfffd:(int)cp;
}
static unsigned font_index(unsigned cp)
{
    if(cp>=32&&cp<127)return cp-32;
    unsigned lo=95,hi=BILI_FONT_COUNT;while(lo<hi){unsigned m=(lo+hi)/2;if(bili_font_cp[m]<cp)lo=m+1;else hi=m;}
    return lo<BILI_FONT_COUNT&&bili_font_cp[lo]==cp?lo:BILI_FONT_COUNT;
}
static int zero_width(unsigned cp){return (cp>=0xfe00&&cp<=0xfe0f)||(cp>=0x200b&&cp<=0x200f)||cp==0xfeff;}
static unsigned bu_advance(unsigned cp)
{unsigned g=font_index(cp);return zero_width(cp)?0:g<BILI_FONT_COUNT?bili_font_width[g]:16;}
static void rect(int x,int y,int w,int h,uint16_t color)
{
    int top=y<clip_top?clip_top:y,bottom=y+h>clip_bottom?clip_bottom:y+h,left=x<0?0:x,right=x+w>480?480:x+w;
    if(top<0)top=0;if(bottom>272)bottom=272;
    for(int r=top;r<bottom;r++)for(int c=left;c<right;c++)pixels[r*480+c]=color;
}
static void glyph(unsigned cp,int x,int y,int right,uint16_t color)
{
    if(zero_width(cp))return;unsigned g=font_index(cp);if(g==BILI_FONT_COUNT)g=font_index(0x25a1);
    int w=bili_font_width[g],top=y<clip_top?clip_top:y,bottom=y+16>clip_bottom?clip_bottom:y+16,left=x<0?-x:0,end=w;
    if(top<0)top=0;if(bottom>272)bottom=272;if(right>480)right=480;if(x+end>right)end=right-x;
    if(top>=bottom||left>=end)return;
    for(int r=top;r<bottom;r++){unsigned row=bili_font_rows[g][r-y];if(!row)continue;uint16_t *dest=pixels+r*480;
        for(int c=left;c<end;c++)if(row&(0x8000u>>c))dest[x+c]=color;}
}
static const char *draw_text(const char *s,int *x,int y,int right,uint16_t color)
{
    while(*s){const char *next=s;unsigned cp=bu_utf8_string(&next),w=bu_advance(cp);if(*x+(int)w>right)break;glyph(cp,*x,y,right,color);*x+=w;s=next;}return s;
}
static const char *text(const char *s,int x,int y,int right,uint16_t color)
{return draw_text(s,&x,y,right,color);}
static void text_ellipsis(const char *s,int x,int y,int right,uint16_t color)
{
    if(right<=x)return;
    const char *p=s;unsigned width=0;while(*p&&width<=(unsigned)(right-x)){unsigned cp=bu_utf8_string(&p);width+=bu_advance(cp);}
    if(!*p&&width<=(unsigned)(right-x)){text(s,x,y,right,color);return;}
    draw_text(s,&x,y,right-16,color);glyph(0x2026,x,y,right,color);
}
