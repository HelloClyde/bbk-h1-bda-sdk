#ifndef H1_NETSURF_PLATFORM_H
#define H1_NETSURF_PLATFORM_H
#include <stdint.h>
uint32_t h1ns_time_ms(void);
#include <stddef.h>
extern volatile uint32_t h1ns_heap_live,h1ns_heap_peak,h1ns_log_size;
extern char h1ns_log[32768];
/* Optional probe: magic, enabled, stage, sequence, result, bytes, ms, JS errors. */
extern volatile uint32_t h1ns_diag_state[8];
void h1ns_diag_event(unsigned stage,const char *source,int result,unsigned bytes);
void h1ns_panic(int code);
void h1ns_heap_release(void);
void h1ns_heap_trim(void);
size_t h1ns_malloc_usable_size(const void *p);
void *h1ns_malloc(size_t n);
void *h1ns_calloc(size_t count,size_t n);
void *h1ns_realloc(void *p,size_t n);
void h1ns_free(void *p);
extern volatile uint32_t h1ns_oom_state[4],h1ns_heap_reserved,h1ns_heap_arena_size,h1ns_heap_integrity;
extern volatile uint32_t h1ns_heap_blocks,h1ns_heap_peak_blocks,h1ns_heap_abandoned;
extern volatile uint32_t h1ns_heap_growth[2];
void h1ns_io_release(void);
void h1ns_io_init(void);
#endif
