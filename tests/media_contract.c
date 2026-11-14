#include "h1_draw.h"
#include "h1_aac.h"
#include "h1net_wire.h"
#include <assert.h>
#include <string.h>
typedef struct {unsigned id,offset,sequence,fragments,busy,bad,cancel;uint32_t now;} peer;
static unsigned features(void *p){(void)p;return H1NET_CAP_AUDIO_AAC;}
static uint32_t now(void *p){peer *s=p;return s->now+=100;}
static int cancelled(void *p){return ((peer *)p)->cancel;}
static int rpc(void *ctx,unsigned op,const void *in,unsigned n,void *out,unsigned cap,unsigned *got)
{
    peer *p=ctx;const uint8_t *b=in;uint8_t *r=out;(void)cap;*got=0;
    if(op==H1NET_AUDIO_OPEN){h1net_put32(r,p->id);*got=4;return 0;}
    assert(h1net_u32(b)==p->id);
    if(op==H1NET_AUDIO_PUSH){
        if(p->busy){p->busy--;return -102;}
        assert(h1net_u32(b+4)==p->sequence&&h1net_u32(b+12)==p->offset);
        p->offset+=n-20;p->fragments++;h1net_put32(r,p->offset+p->bad);*got=4;
        if(p->offset==h1net_u32(b+8)){p->offset=0;p->sequence++;}return 0;
    }
    if(op==H1NET_AUDIO_READ){memset(r,0,16);h1net_put32(r,p->id);h1net_put32(r+8,4);*got=16;return 0;}
    if(op==H1NET_AUDIO_INFO){memset(r,0,32);h1net_put32(r,p->id);h1net_put32(r+4,2);h1net_put32(r+8,32768);h1net_put32(r+12,1);h1net_put32(r+20,4);h1net_put32(r+24,p->bad?3:0);*got=32;return 0;}
    return 0;
}
int main(void)
{
    uint16_t memory[2+7*19];for(unsigned i=0;i<sizeof memory/2;i++)memory[i]=0x1234;
    const uint16_t cp[]={65,0x25a1};const uint8_t widths[]={8,16};uint16_t rows[2][16]={{0x8000},{0xffff}};
    h1_font16 font={2,cp,widths,(const uint16_t (*)[16])rows};
    h1_canvas c={memory+1,16,7,19,1,6,&font};
    h1_draw_rect(&c,-2,-5,100,100,1);
    for(int y=0;y<7;y++)for(int x=0;x<19;x++)assert(memory[1+y*19+x]==(y>=1&&y<6&&x<16?1:0x1234));
    assert(memory[0]==0x1234&&memory[1+7*19]==0x1234);
    h1_draw_glyph(&c,'A',0,1,16,2);assert(memory[1+19]==2&&memory[2+19]==1);
    h1_draw_glyph(&c,0x10ffff,0,2,16,3);assert(memory[1+2*19+15]==3);
    assert(h1_draw_width(&font,"A\xe2\x80\x8b")==8);
    const char *s="\xf0\x80\x80\x80";assert(h1_utf8_next(&s)==0xfffd);
    h1_font16 empty={0,0,0,0};c.font=&empty;h1_draw_glyph(&c,300,0,0,16,9);
    peer p={.id=7},q={.id=9};h1_aac a,b;
    h1_aac_driver d={&p,rpc,features,now,0,cancelled};h1_aac_init(&a,&d,0);d.context=&q;h1_aac_init(&b,&d,0);
    uint8_t asc[]={0x14,0x08},packet[1000]={0},pcm[4];unsigned got=99;
    assert(h1_aac_open(&a,asc,2)&&h1_aac_open(&b,asc,2));
    p.busy=2;assert(h1_aac_push(&a,packet,1000,0)==0&&p.fragments==3&&a.sequence==1&&b.sequence==0);
    assert(h1_aac_read(&a,pcm,4,&got)==0&&got==4&&a.output_offset==4&&b.output_offset==0);
    assert(h1_aac_eof(&a)==1);p.bad=1;h1_aac_info info;assert(h1_aac_get_info(&a,&info)==-3);p.bad=0;
    p.busy=100;assert(h1_aac_push(&a,packet,10,0)==-2);p.cancel=1;assert(h1_aac_push(&a,packet,10,0)==-6);
    p.busy=p.cancel=0;p.bad=1;assert(h1_aac_push(&a,packet,10,0)==-3);
    a.in_progress=1;assert(h1_aac_reset(&a)==-3);h1_aac_close(&a);assert(a.job==7);a.in_progress=0;
    h1_aac_close(&a);assert(!a.job&&b.job==9);h1_aac_close(&b);
    return 0;
}
