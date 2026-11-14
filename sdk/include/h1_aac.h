#ifndef H1_AAC_H
#define H1_AAC_H
#include <stdint.h>
/* Generic H1NET AAC offload. Independent caller-owned sessions; ESP firmware
 * supports one active decoder. RPC returns 0, -102 BUSY or a negative error.
 * now_ms must advance during idle; callbacks must not recursively PUSH/reset/close.
 * No HTTP provider, account, filesystem or application state is used here. */
typedef struct {
    void *context;
    int (*rpc)(void *,unsigned,const void *,unsigned,void *,unsigned,unsigned *);
    unsigned (*features)(void *);
    uint32_t (*now_ms)(void *);
    void (*idle)(void *);
    int (*cancelled)(void *);
} h1_aac_driver;
typedef struct {
    h1_aac_driver driver;
    uint32_t job,sequence,output_offset;
    int (*progress_pcm)(void *);
    void *progress_context;
    int in_progress;
    volatile uint32_t *state;
    uint32_t diagnostics[16];
} h1_aac;
typedef struct {unsigned state,produced,buffered;} h1_aac_info;
/* Call once before use. Optional external diagnostics contains 16 words.
 * magic/schema/requested/capability/active/job/error/input/output/frames/reads/
 * empty/finished/PCM FNV/resets/reserved. Never contains credentials. */
void h1_aac_init(h1_aac *,const h1_aac_driver *,volatile uint32_t *diagnostics);
/* open returns 1 success, 0 failure; fixed PCM S16LE 32768Hz mono.
 * ASC 1..64 bytes, packet 1..16384 bytes, read even capacity >=2.
 * Other operations return 0 or negative, eof returns 1 drained / 0 pending. */
int h1_aac_open(h1_aac *,const uint8_t *,unsigned);
int h1_aac_push(h1_aac *,const uint8_t *,unsigned,uint32_t);
int h1_aac_read(h1_aac *,uint8_t *,unsigned,unsigned *);
int h1_aac_reset(h1_aac *);
int h1_aac_finish(h1_aac *);
int h1_aac_eof(h1_aac *);
int h1_aac_get_info(h1_aac *,h1_aac_info *);
void h1_aac_close(h1_aac *);
/* Progress may only drain PCM; >0 bytes drained resets BUSY deadline. */
void h1_aac_set_progress(h1_aac *,int (*)(void *),void *);
#endif
