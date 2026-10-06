#ifndef H1_PCM_STREAM_H
#define H1_PCM_STREAM_H
#include "../h1_audio.h"
/* V1.41 ONLY, single owner, foreground calls serialized by the application.
 * Read-only internal queue inspection is NOT a vendor ABI. Signature mismatch
 * disables streaming. Call poll more often than one ring traversal; complete
 * missed traversals are indistinguishable from no movement. PCM is s16 mono. */
typedef struct {
    h1_pcm_api api;
    h1_pcm_descriptor descriptor;
    volatile h1_u32 *queue;
    short *samples;
    h1_u32 capacity,prefill,queued,write_position,read_position;
    unsigned initialized,opened,paused,playing,underruns,failures;
} h1_pcm_stream;
static inline volatile h1_u32 *h1_pcm_v141_queue(h1_pcm_api *a)
{
    const h1_u32 *c=(const h1_u32 *)a->submit;h1_u32 p=(h1_u32)c;
    if ((p&3u) || p<0x80000000u || p>0x83ffff80u ||
        (c[0x70/4]&0xffff0000u)!=0x3c060000u ||
        (c[0x74/4]&0xffff0000u)!=0x24c60000u || c[0x78/4]!=0x00c43021u) return 0;
    p=((c[0x70/4]&65535u)<<16)+(short)c[0x74/4];
    return (p&3u) || p<0x80000000u || p>0x83fffffcu ? 0 : (volatile h1_u32 *)p;
}
static inline void h1_pcm_stream_reset(h1_pcm_stream *s)
{
    unsigned i;
    (void)s->api.submit(0,0,0,0); /* cancel references before clearing storage */
    s->playing=s->queued=s->write_position=s->read_position=0;
    for (i=0;i<s->capacity;++i) s->samples[i]=0;
}
static inline void h1_pcm_stream_close(h1_pcm_stream *s)
{
    if (!s) return;
    if (s->opened) {
        (void)s->api.stop();(void)s->api.submit(0,0,0,0);(void)s->api.close();
    }
    if (s->initialized) (void)s->api.descriptor_destroy(&s->descriptor);
    s->opened=s->initialized=s->paused=s->playing=0;s->queued=0;s->queue=0;
}
/* Zero-initialize s once. Do not reopen a live stream; close first.
 * Recommended 32000 Hz, 8192 samples, 4096 sample lead (~128 ms). */
static inline int h1_pcm_stream_open(h1_pcm_stream *s,short *samples,
                                    h1_u32 count,h1_u32 prefill,h1_u32 rate)
{
    h1_pcm_config cfg={0};unsigned i;
    if (!s || s->opened || s->initialized || !samples || ((h1_u32)samples&31u) ||
        count<2 || count>32768 || !prefill || prefill>=count || !rate) return 0;
    if (!h1_pcm_api_get(&s->api)) return 0;
    s->queue=h1_pcm_v141_queue(&s->api);
    if (!s->queue || !h1_pcm_descriptor_prepare(&s->descriptor,samples,count*2)) return 0;
    s->samples=samples;s->capacity=count;s->prefill=prefill;
    s->queued=s->write_position=s->read_position=s->playing=s->paused=0;
    s->underruns=s->failures=0;
    for (i=0;i<count;++i) samples[i]=0;
    if (!s->api.descriptor_init(&s->descriptor)) return 0;
    s->initialized=1;cfg.sample_rate=rate;cfg.mode=1;cfg.buffer_bytes=4096;
    if (!s->api.device_open(&cfg)) { h1_pcm_stream_close(s);return 0; }
    s->opened=1;
    if (!s->api.start()) { h1_pcm_stream_close(s);return 0; }
    return 1;
}
/* Returns 1 for valid progress, -1 after recovering an underrun/invalid node,
 * 0 for inactive output. Exposes queued samples without changing firmware. */
static inline int h1_pcm_stream_poll(h1_pcm_stream *s)
{
    volatile h1_u32 *node;h1_u32 p,cursor,base,position,consumed,i;
    if (!s || !s->opened || s->paused) return 0;
    if (!s->playing) return 1;
    p=s->queue[0];node=(volatile h1_u32 *)p;base=(h1_u32)s->samples;
    if ((p&3u) || p<0x80000000u || p>0x83ffffe8u || node[1]!=(h1_u32)&s->descriptor) goto invalid;
    cursor=node[2];
    if (cursor<base || cursor>base+s->capacity*2 || (cursor&1u)) goto invalid;
    position=((cursor-base)/2)%s->capacity;
    consumed=(position+s->capacity-s->read_position)%s->capacity;
    if (consumed>s->queued) { ++s->underruns;h1_pcm_stream_reset(s);return -1; }
    for (i=0;i<consumed;++i) s->samples[(s->read_position+i)%s->capacity]=0;
    s->read_position=position;s->queued-=consumed;return 1;
invalid:
    ++s->failures;h1_pcm_stream_reset(s);return -1;
}
/* Returns accepted samples, preserving unread audio; caller handles excess. */
static inline h1_u32 h1_pcm_stream_write(h1_pcm_stream *s,const short *pcm,h1_u32 count)
{
    h1_u32 i=0;
    if (!s || !s->opened || s->paused || !pcm) return 0;
    (void)h1_pcm_stream_poll(s);
    while (i<count && s->queued<s->capacity) {
        s->samples[s->write_position]=pcm[i++];
        s->write_position=(s->write_position+1)%s->capacity;++s->queued;
        if (!s->playing && s->queued>=s->prefill) {
            if (!s->api.submit(0,&s->descriptor,0,0)) {
                ++s->failures;h1_pcm_stream_reset(s);break;
            }
            s->playing=1;
        }
    }
    return i;
}
static inline void h1_pcm_stream_pause(h1_pcm_stream *s)
{
    if (!s || !s->opened || s->paused) return;
    (void)s->api.stop();h1_pcm_stream_reset(s);s->paused=1;
}
static inline int h1_pcm_stream_resume(h1_pcm_stream *s)
{
    if (!s || !s->opened) return 0;
    if (!s->paused) return 1;
    if (!s->api.start()) { h1_pcm_stream_close(s);return 0; }
    s->paused=0;return 1; /* write re-prefills; device/descriptor stay allocated */
}
#endif
