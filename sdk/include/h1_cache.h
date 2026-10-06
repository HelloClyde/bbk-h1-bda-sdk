#ifndef H1_CACHE_H
#define H1_CACHE_H
#include "h1_types.h"
/* Privileged JZ4740 KSEG0 executable RAM only. Write back D before
 * invalidating I; 16-byte coverage works for the researched cache lines.
 * Does not make a translator ABI-safe: firmware IRQs can restore host GP. */
static inline int h1_code_cache_sync(void *start, void *end)
{
    h1_u32 first=(h1_u32)start,limit=(h1_u32)end,address;
    if (first>=limit) return first==limit;
    if (first<0x80000000u || limit>0x84000000u) return 0;
    first&=~15u;limit=(limit+15u)&~15u;
    __asm__ volatile("sync" ::: "memory");
    for (address=first;address<limit;address+=16u)
        __asm__ volatile("cache 0x15, 0(%0)" :: "r"(address) : "memory");
    __asm__ volatile("sync" ::: "memory");
    for (address=first;address<limit;address+=16u)
        __asm__ volatile("cache 0x10, 0(%0)" :: "r"(address) : "memory");
    __asm__ volatile("sync\n\tnop\n\tnop\n\tnop" ::: "memory");return 1;
}
#endif
