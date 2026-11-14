/* Single-threaded browser heap. Reserve once, then reuse/coalesce with TLSF. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "platform.h"
#include "../third_party/tlsf/tlsf.h"
#ifdef H1_HEAP_TEST
extern void *h1_heap_test_alloc(size_t);
extern void h1_heap_test_free(void *);
#define h1_alloc h1_heap_test_alloc
#define h1_free h1_heap_test_free
#else
#include "h1_sdk.h"
#endif

volatile uint32_t h1ns_heap_live,h1ns_heap_peak,h1ns_oom_state[4];
/* Live counts actual usable block bytes. Reserved includes all TLSF overhead. */
volatile uint32_t h1ns_heap_reserved,h1ns_heap_arena_size,h1ns_heap_integrity;
volatile uint32_t h1ns_heap_blocks,h1ns_heap_peak_blocks,h1ns_heap_abandoned;
volatile uint32_t h1ns_heap_growth[2];
static tlsf_t arena;
static void *backing;
static int attempted;
static struct {void *raw;pool_t pool;size_t size;} extra[16];
#define HEAP_RESERVE_MAX (43u*512*1024)
static size_t storage_size(size_t n)
{
    /* Make the complete TLSF block stride a multiple of eight, including
       its size header. This avoids repeated tiny alignment gaps on o32. */
    return ((n+sizeof(size_t)+7)&~(size_t)7)-sizeof(size_t);
}

static int heap_init(void)
{
    if(attempted)return arena!=NULL;
    attempted=1;
    /* Keep SDK/native input RAM available on ordinary pages. Larger pages
       add a few pools on demand, up to the measured safe 21.5MiB ceiling. */
    static const size_t sizes[]={12u*1024*1024,8u*1024*1024,4u*1024*1024};
    for(unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++){
        backing=h1_alloc(sizes[i]);
        if(!backing || backing==(void *)-1){backing=NULL;continue;}
        arena=tlsf_create_with_pool(backing,sizes[i]);
        if(arena){h1ns_heap_reserved=h1ns_heap_arena_size=sizes[i];return 1;}
        h1_free(backing);backing=NULL;
    }
    return 0;
}

static int heap_grow(size_t request)
{
    unsigned slot;for(slot=0;slot<16 && extra[slot].raw;slot++){}
    if(slot==16 || h1ns_heap_reserved>=HEAP_RESERVE_MAX)return 0;
    size_t available=HEAP_RESERVE_MAX-h1ns_heap_reserved;
    size_t minimum=request+request/16+4096; /* TLSF size-class rounding. */
    minimum=(minimum+65535)&~(size_t)65535;
    size_t bytes=minimum<4u*1024*1024?4u*1024*1024:minimum;
    if(bytes>available)bytes=available;
    while(bytes>=minimum){
        void *raw=h1_alloc(bytes);
        if(raw && raw!=(void *)-1){
            pool_t pool=tlsf_add_pool(arena,raw,bytes);
            if(pool){
                extra[slot].raw=raw;extra[slot].pool=pool;extra[slot].size=bytes;
                h1ns_heap_reserved+=bytes;
                if(h1ns_heap_reserved>h1ns_heap_arena_size)h1ns_heap_arena_size=h1ns_heap_reserved;
                return 1;
            }
            h1_free(raw);return 0;
        }
        ++h1ns_heap_growth[0];h1ns_heap_growth[1]=bytes;
        bytes=(bytes/2)&~(size_t)65535;
    }
    return 0;
}

static void pool_used(void *ptr,size_t n,int used,void *context)
{
    (void)ptr;(void)n;if(used)*(int *)context=1;
}
void h1ns_heap_trim(void)
{
    if(!arena)return;
    for(unsigned i=0;i<16;i++)if(extra[i].raw){
        int used=0;tlsf_walk_pool(extra[i].pool,pool_used,&used);
        if(!used){
            tlsf_remove_pool(arena,extra[i].pool);h1_free(extra[i].raw);
            h1ns_heap_reserved-=extra[i].size;memset(&extra[i],0,sizeof extra[i]);
        }
    }
}

static void failed(unsigned kind,size_t n)
{
    ++h1ns_oom_state[0];h1ns_oom_state[1]=kind;
    h1ns_oom_state[2]=n;h1ns_oom_state[3]=h1ns_heap_live;errno=ENOMEM;
}
static void account(size_t old,size_t n)
{
    h1ns_heap_live=h1ns_heap_live-old+n;
    if(h1ns_heap_peak<h1ns_heap_live)h1ns_heap_peak=h1ns_heap_live;
}
size_t h1ns_malloc_usable_size(const void *p)
{
    return p?tlsf_block_size((void *)p):0;
}
void *h1ns_malloc(size_t n)
{
    if(!n)n=1;
    if(n>24u*1024*1024 || h1ns_heap_live>24u*1024*1024-n){failed(1,n);return NULL;}
    if(!heap_init()){failed(2,n);return NULL;}
    void *p=tlsf_memalign(arena,8,storage_size(n));
    if(!p && heap_grow(storage_size(n)))p=tlsf_memalign(arena,8,storage_size(n));
    if(!p){failed(3,n);return NULL;}
    account(0,tlsf_block_size(p));
    if(++h1ns_heap_blocks>h1ns_heap_peak_blocks)h1ns_heap_peak_blocks=h1ns_heap_blocks;
    return p;
}
void h1ns_free(void *v)
{
    if(!v)return;
    account(tlsf_block_size(v),0);--h1ns_heap_blocks;tlsf_free(arena,v);
}
void *h1ns_calloc(size_t count,size_t n)
{
    if(n && count>SIZE_MAX/n){failed(1,SIZE_MAX);return NULL;}
    void *p=h1ns_malloc(count*n);if(p)memset(p,0,count*n);return p;
}
void *h1ns_realloc(void *v,size_t n)
{
    if(!v)return h1ns_malloc(n);
    if(!n){h1ns_free(v);return NULL;}
    size_t size=tlsf_block_size(v);
    if(n>24u*1024*1024 || h1ns_heap_live-size>24u*1024*1024-n){failed(1,n);return NULL;}
    void *p=tlsf_realloc(arena,v,storage_size(n));
    if(!p && heap_grow(storage_size(n)))p=tlsf_realloc(arena,v,storage_size(n));
    if(!p){failed(3,n);return NULL;}
    account(size,tlsf_block_size(p));return p;
}
void h1ns_heap_release(void)
{
    /* The complete arena belongs to this BDA. Keep a truthful record of any
       remaining library allocations before returning the whole SDK block. */
    h1ns_heap_abandoned=h1ns_heap_live;
    if(arena){
        h1ns_heap_integrity=tlsf_check(arena)|tlsf_check_pool(tlsf_get_pool(arena));
        for(unsigned i=0;i<16;i++)if(extra[i].raw)h1ns_heap_integrity|=tlsf_check_pool(extra[i].pool);
        tlsf_destroy(arena);
    }
    for(unsigned i=0;i<16;i++)if(extra[i].raw){h1_free(extra[i].raw);memset(&extra[i],0,sizeof extra[i]);}
    if(backing)h1_free(backing);
    arena=NULL;backing=NULL;attempted=0;h1ns_heap_reserved=0;h1ns_heap_live=0;h1ns_heap_blocks=0;
}
