/* Stateful AAC offload over the same generic H1NET bus as HTTP. One audio
 * session is independent of the four HTTP jobs. The worker has no provider/account logic. */
#include "h1_aac.h"
#include "h1net_wire.h"
#include "h1net_client.h"
#include <string.h>
void h1_aac_init(h1_aac *c,const h1_aac_driver *driver,volatile uint32_t *diagnostics)
{
    memset(c,0,sizeof *c);if(driver)c->driver=*driver;
    c->state=diagnostics?diagnostics:c->diagnostics;
    for(unsigned i=0;i<16;i++)c->state[i]=0;
    c->state[0]=0x31415242u;c->state[1]=1;
}
void h1_aac_set_progress(h1_aac *c,int (*fn)(void *),void *context)
{c->progress_pcm=fn;c->progress_context=context;}
static int call(h1_aac *c,unsigned op,const void *p,unsigned n,void *out,unsigned capacity,unsigned *used)
{
    int e=c->driver.rpc(c->driver.context,op,p,n,out,capacity,used);
    /* BUSY explicitly leaves a fragment unconsumed; the PUSH retry below
       is normal backpressure, not a sticky decoder/transport failure. */
    if(e&&e!=-102)c->state[6]=(uint32_t)e;
    return e;
}
static int job_call(h1_aac *c,unsigned op)
{
    uint8_t p[4];unsigned used=0;h1net_put32(p,c->job);
    int e=call(c,op,p,4,NULL,0,&used);return e?e:used?-3:0;
}
void h1_aac_close(h1_aac *c)
{
    if(c->in_progress)return;
    if(c->job)(void)job_call(c,H1NET_AUDIO_CLOSE);
    c->job=c->sequence=c->output_offset=0;c->state[4]=c->state[5]=0;
    c->progress_pcm=NULL;c->progress_context=NULL;c->in_progress=0;
}
int h1_aac_open(h1_aac *c,const uint8_t *asc,unsigned n)
{
    if(c->in_progress)return 0;
    h1_aac_close(c);for(unsigned i=2;i<16;i++)c->state[i]=0;
    c->state[2]=1;
    if(!c->driver.rpc||!c->driver.features||!c->driver.now_ms)return 0;
    unsigned features=c->driver.features(c->driver.context);c->state[3]=!!(features&H1NET_CAP_AUDIO_AAC);
    if(!(features&H1NET_CAP_AUDIO_AAC)||!asc||!n||n>64)return 0;
    uint8_t p[80],reply[4];unsigned used=0;
    h1net_put32(p,1);h1net_put32(p+4,32768);h1net_put32(p+8,1);h1net_put32(p+12,n);memcpy(p+16,asc,n);
    int e=call(c,H1NET_AUDIO_OPEN,p,16+n,reply,4,&used);
    if(e||used!=4||!(c->job=h1net_u32(reply)))return 0;
    c->sequence=c->output_offset=0;c->state[4]=1;c->state[5]=c->job;c->state[13]=2166136261u;return 1;
}
int h1_aac_push(h1_aac *c,const uint8_t *data,unsigned n,uint32_t pts)
{
    if(!c->job||!data||!n||n>16384||c->in_progress)return -3;
    uint8_t p[512],reply[4];unsigned offset=0;
    while(offset<n){
        unsigned count=n-offset;if(count>492)count=492;
        h1net_put32(p,c->job);h1net_put32(p+4,c->sequence);h1net_put32(p+8,n);h1net_put32(p+12,offset);h1net_put32(p+16,pts);memcpy(p+20,data+offset,count);
        unsigned used=0;uint32_t start=c->driver.now_ms(c->driver.context);int e;
        do{
            e=call(c,H1NET_AUDIO_PUSH,p,20+count,reply,4,&used);
            if(e!=-102)break;
            if(c->driver.cancelled&&c->driver.cancelled(c->driver.context))return -6;
            if(c->progress_pcm){
                c->in_progress=1;int advanced=c->progress_pcm(c->progress_context);c->in_progress=0;
                if(advanced<0){c->state[6]=(uint32_t)advanced;return advanced;}
                if(advanced)start=c->driver.now_ms(c->driver.context);
            }
            if(c->driver.idle)c->driver.idle(c->driver.context);
        }while(c->driver.now_ms(c->driver.context)-start<1000);
        if(e==-102){c->state[6]=(uint32_t)H1NET_TIMEOUT;return H1NET_TIMEOUT;}
        if(e)return e;if(used!=4||h1net_u32(reply)!=offset+count)return -3;offset+=count;
    }
    ++c->sequence;++c->state[9];c->state[7]+=n;return 0;
}
int h1_aac_read(h1_aac *c,uint8_t *out,unsigned capacity,unsigned *received)
{
    if(!c->job||!out||!received||capacity<2)return -3;*received=0;capacity&=~1u;
    while(*received<capacity){
        unsigned wanted=capacity-*received;if(wanted>500)wanted=500;
        uint8_t p[8],reply[512];unsigned used=0;
        h1net_put32(p,c->job);h1net_put32(p+4,wanted);
        int e=call(c,H1NET_AUDIO_READ,p,8,reply,512,&used);if(e)return e;
        ++c->state[10];
        if(used<12||h1net_u32(reply)!=c->job||h1net_u32(reply+4)!=c->output_offset)return -3;
        unsigned n=h1net_u32(reply+8);if(n>wanted||used!=12+n||(n&1))return -3;
        if(!n){++c->state[11];break;}
        memcpy(out+*received,reply+12,n);*received+=n;c->output_offset+=n;
        uint32_t hash=c->state[13];for(unsigned i=0;i<n;i++)hash=(hash^reply[12+i])*16777619u;c->state[13]=hash;
        if(n<wanted)break;
    }
    c->state[8]=c->output_offset;return 0;
}
int h1_aac_reset(h1_aac *c)
{
    if(c->in_progress)return -3;
    if(!c->job)return 0;int e=job_call(c,H1NET_AUDIO_RESET);if(e)return e;
    c->sequence=c->output_offset=0;for(unsigned i=6;i<14;i++)c->state[i]=0;c->state[13]=2166136261u;++c->state[14];return 0;
}
int h1_aac_finish(h1_aac *c)
{
    if(c->in_progress)return -3;
    if(!c->job||c->state[12])return 0;
    int e=job_call(c,H1NET_AUDIO_FINISH);if(!e)c->state[12]=1;return e;
}
int h1_aac_get_info(h1_aac *c,h1_aac_info *out)
{
    if(!c->job||!out)return -3;
    uint8_t p[4],reply[32];unsigned used=0;h1net_put32(p,c->job);
    int e=call(c,H1NET_AUDIO_INFO,p,4,reply,32,&used);if(e)return e;
    if(used!=32)return -3;
    unsigned state=h1net_u32(reply+4),produced=h1net_u32(reply+20),buffered=h1net_u32(reply+24);
    if(used!=32||h1net_u32(reply)!=c->job||h1net_u32(reply+8)!=32768||h1net_u32(reply+12)!=1||
        produced<c->output_offset||buffered!=produced-c->output_offset||buffered>131072||state<1||state>3)return -3;
    if(state==3)return -5;
    out->state=state;out->produced=produced;out->buffered=buffered;return 0;
}
int h1_aac_eof(h1_aac *c)
{
    if(!c->job)return 1;h1_aac_info info;int e=h1_aac_get_info(c,&info);
    return e?e:info.state==2&&!info.buffered;
}
