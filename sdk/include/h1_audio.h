#ifndef H1_AUDIO_H
#define H1_AUDIO_H
#include "h1_runtime.h"
/* Native V1.41 PCM services; offsets are SYS, not GUI. Private descriptor
 * words belong to firmware until destroy. PCM and descriptor: 32-byte aligned.
 * Names are descriptive, not recovered vendor symbols. Unknown flags/modes
 * deliberately remain integers rather than invented public enums. */
typedef struct __attribute__((aligned(32))) {
    void *pcm;
    h1_u32 bytes;
    h1_u32 private_words[6];
} h1_pcm_descriptor;
typedef struct { h1_u32 sample_rate,mode,buffer_bytes,reserved[6]; } h1_pcm_config;
typedef struct {
    int (*descriptor_init)(h1_pcm_descriptor *);
    int (*descriptor_destroy)(h1_pcm_descriptor *);
    int (*device_open)(h1_pcm_config *);
    int (*submit)(int,h1_pcm_descriptor *,int,int);
    int (*start)(void),(*stop)(void),(*close)(void);
} h1_pcm_api;
static inline int h1_pcm_api_get(h1_pcm_api *a)
{
    void *t=h1_runtime_table(H1_RUNTIME_SYS_TABLE_SLOT);
    if (!a) return 0;
    a->descriptor_init=(int (*)(h1_pcm_descriptor *))h1_runtime_entry(t,0x50u);
    a->descriptor_destroy=(int (*)(h1_pcm_descriptor *))h1_runtime_entry(t,0x54u);
    a->device_open=(int (*)(h1_pcm_config *))h1_runtime_entry(t,0x58u);
    a->submit=(int (*)(int,h1_pcm_descriptor *,int,int))h1_runtime_entry(t,0x5Cu);
    a->start=(int (*)(void))h1_runtime_entry(t,0x60u);
    a->stop=(int (*)(void))h1_runtime_entry(t,0x64u);
    a->close=(int (*)(void))h1_runtime_entry(t,0x68u);
    return a->descriptor_init && a->descriptor_destroy && a->device_open &&
        a->submit && a->start && a->stop && a->close;
}
/* Clear only BEFORE init, never while a descriptor is live. */
static inline int h1_pcm_descriptor_prepare(h1_pcm_descriptor *d,void *pcm,h1_u32 bytes)
{
    unsigned i;
    if (!d || !pcm || ((h1_u32)d&31u) || ((h1_u32)pcm&31u) || !bytes || (bytes&1u)) return 0;
    d->pcm=pcm;d->bytes=bytes;
    for (i=0;i<6;++i) d->private_words[i]=0;
    return 1;
}
#endif
