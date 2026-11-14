#include "h1_draw.h"
int h1_utf8_next(const char **s)
{
    const uint8_t *p=(const uint8_t *)*s;unsigned b=*p,n=b<128?1:b>=194&&b<=223?2:b>=224&&b<=239?3:b>=240&&b<=244?4:0;
    if(!b)return -1;if(!n){++*s;return 0xfffd;}
    unsigned cp=n==1?b:b&((1u<<(7-n))-1u);
    for(unsigned i=1;i<n;++i){if(!p[i]||(p[i]&192)!=128){++*s;return 0xfffd;}cp=(cp<<6)|(p[i]&63);}
    *s+=n;return (n==2&&cp<128)||(n==3&&cp<2048)||(n==4&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)?0xfffd:(int)cp;
}
static unsigned font_index(const h1_font16 *f,unsigned cp)
{
    unsigned lo=0,hi=f->count;while(lo<hi){unsigned m=(lo+hi)/2;if(f->codepoints[m]<cp)lo=m+1;else hi=m;}
    return lo<f->count&&f->codepoints[lo]==cp?lo:f->count;
}
static int zero_width(unsigned cp){return (cp>=0xfe00&&cp<=0xfe0f)||(cp>=0x200b&&cp<=0x200f)||cp==0xfeff;}
unsigned h1_draw_advance(const h1_font16 *f,unsigned cp)
{unsigned g=font_index(f,cp);return zero_width(cp)?0:g<f->count?f->widths[g]:16;}
void h1_draw_rect(h1_canvas *c,int x,int y,int w,int h,uint16_t color)
{
    if(w<=0||h<=0)return;
    int top=y<c->clip_top?c->clip_top:y,bottom=y+h>c->clip_bottom?c->clip_bottom:y+h,left=x<0?0:x,right=x+w>c->width?c->width:x+w;
    if(top<0)top=0;if(bottom>c->height)bottom=c->height;
    for(int r=top;r<bottom;r++)for(int col=left;col<right;col++)c->pixels[r*c->stride+col]=color;
}
void h1_draw_glyph(h1_canvas *c,unsigned cp,int x,int y,int right,uint16_t color)
{
    const h1_font16 *f=c->font;
    if(zero_width(cp))return;unsigned g=font_index(f,cp);if(g==f->count)g=font_index(f,0x25a1);
    if(g==f->count)return;
    int w=f->widths[g],top=y<c->clip_top?c->clip_top:y,bottom=y+16>c->clip_bottom?c->clip_bottom:y+16,left=x<0?-x:0,end=w;
    if(top<0)top=0;if(bottom>c->height)bottom=c->height;if(right>c->width)right=c->width;if(x+end>right)end=right-x;
    if(top>=bottom||left>=end)return;
    for(int r=top;r<bottom;r++){unsigned row=f->rows[g][r-y];if(!row)continue;uint16_t *dest=c->pixels+r*c->stride;
        for(int c=left;c<end;c++)if(row&(0x8000u>>c))dest[x+c]=color;}
}
const char *h1_draw_text_at(h1_canvas *c,const char *s,int *x,int y,int right,uint16_t color)
{
    while(*s){const char *next=s;unsigned cp=h1_utf8_next(&next),w=h1_draw_advance(c->font,cp);if(*x+(int)w>right)break;h1_draw_glyph(c,cp,*x,y,right,color);*x+=w;s=next;}return s;
}
const char *h1_draw_text(h1_canvas *c,const char *s,int x,int y,int right,uint16_t color)
{return h1_draw_text_at(c,s,&x,y,right,color);}
void h1_draw_ellipsis(h1_canvas *c,const char *s,int x,int y,int right,uint16_t color)
{
    if(right<=x)return;
    const char *p=s;unsigned width=0;while(*p&&width<=(unsigned)(right-x)){unsigned cp=h1_utf8_next(&p);width+=h1_draw_advance(c->font,cp);}
    if(!*p&&width<=(unsigned)(right-x)){h1_draw_text(c,s,x,y,right,color);return;}
    h1_draw_text_at(c,s,&x,y,right-16,color);h1_draw_glyph(c,0x2026,x,y,right,color);
}

void h1_draw_round(h1_canvas *c,int x,int y,int w,int h,int radius,uint16_t color)
{
    if(w<=0||h<=0)return;
    if(radius<0)radius=0;if(radius>w/2)radius=w/2;if(radius>h/2)radius=h/2;
    for(int row=0;row<h;row++){
        int d=row<radius?radius-1-row:row>=h-radius?row-h+radius:0,inset=0;
        if(d)while(inset<radius&&(radius-inset)*(radius-inset)+d*d>radius*radius)inset++;
        h1_draw_rect(c,x+inset,y+row,w-2*inset,1,color);
    }
}

void h1_draw_dot(h1_canvas *c,int x,int y,int radius,uint16_t color)
{
    for(int dy=-radius;dy<=radius;dy++)for(int dx=-radius;dx<=radius;dx++)
        if(dx*dx+dy*dy<=radius*radius)h1_draw_rect(c,x+dx,y+dy,1,1,color);
}

void h1_draw_line(h1_canvas *c,int x,int y,int x2,int y2,uint16_t color)
{
    int dx=x2>x?x2-x:x-x2,sx=x<x2?1:-1,dy=y2>y?y-y2:y2-y,sy=y<y2?1:-1,err=dx+dy;
    for(;;){h1_draw_rect(c,x,y,1,1,color);if(x==x2&&y==y2)break;int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}
}


int h1_draw_width(const h1_font16 *f,const char *s)
{int width=0;while(*s)width+=(int)h1_draw_advance(f,(unsigned)h1_utf8_next(&s));return width;}
