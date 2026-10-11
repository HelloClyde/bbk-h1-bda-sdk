#ifndef H1_AUDIO_H
#define H1_AUDIO_H
#include "h1_runtime.h"
/* V1.41 system-wide volume, independent of the PCM device lifecycle.
 * Check the same instruction signatures used by GameBoy v0.12.4 before
 * calling either service. This is not general firmware version detection. */
static inline void *h1_audio_volume_entry(h1_u32 offset)
{
    void *table=h1_runtime_table(H1_RUNTIME_SYS_TABLE_SLOT);
    const h1_u32 *set=(const h1_u32 *)h1_runtime_entry(table,0x40u);
    const h1_u32 *get=(const h1_u32 *)h1_runtime_entry(table,0x44u);
    if (offset!=0x40u && offset!=0x44u) return 0;
    if (((h1_u32)set&3u) || (h1_u32)set<0x80000000u || (h1_u32)set>0x83fffff4u ||
        ((h1_u32)get&3u) || (h1_u32)get<0x80000000u || (h1_u32)get>0x83fffff4u ||
        set[0]!=0x28850065u || set[1]!=0x24030064u ||
        (get[0]&0xffff0000u)!=0x3c020000u || get[1]!=0x03e00008u ||
        (get[2]&0xffff0000u)!=0x8c420000u) return 0;
    return (void *)(offset==0x40u ? set : get);
}
/* Returns 1 when the recognized void setter was called, not a hardware ACK.
 * Clamp signed input to 0..100. Save the original volume before changing it. */
static inline int h1_audio_set_volume(int percent)
{
    typedef void (*fn_type)(int);
    fn_type set=(fn_type)h1_audio_volume_entry(0x40u);
    if (!set) return 0;
    if (percent<0) percent=0;
    if (percent>100) percent=100;
    set(percent);return 1;
}
/* Returns 1 with a 0..100 value; failure leaves the caller's output intact. */
static inline int h1_audio_get_volume(int *percent)
{
    typedef int (*fn_type)(void);
    fn_type get;int value;
    if (!percent) return 0;
    get=(fn_type)h1_audio_volume_entry(0x44u);
    if (!get) return 0;
    value=get();
    if (value<0 || value>100) return 0;
    *percent=value;return 1;
}
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
