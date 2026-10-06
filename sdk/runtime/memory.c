/* Compiler-generated aggregate copies/zeros need these even in freestanding
 * code. Weak symbols allow applications to supply optimized replacements.
 * Volatile byte accesses prevent recursive loop-to-libcall optimization. */
#include "h1_types.h"
__attribute__((weak)) void *memset(void *dst,int value,h1_size_t n)
{
    volatile h1_u8 *p=(volatile h1_u8 *)dst;
    while (n--) *p++=(h1_u8)value;
    return dst;
}
__attribute__((weak)) void *memcpy(void *dst,const void *src,h1_size_t n)
{
    volatile h1_u8 *p=(volatile h1_u8 *)dst;
    const volatile h1_u8 *q=(const volatile h1_u8 *)src;
    while (n--) *p++=*q++;
    return dst;
}
__attribute__((weak)) void *memmove(void *dst,const void *src,h1_size_t n)
{
    volatile h1_u8 *p=(volatile h1_u8 *)dst;
    const volatile h1_u8 *q=(const volatile h1_u8 *)src;
    if ((h1_u32)p<(h1_u32)q) while (n--) *p++=*q++;
    else { p+=n;q+=n;while (n--) *--p=*--q; }
    return dst;
}
__attribute__((weak)) int memcmp(const void *left,const void *right,h1_size_t n)
{
    const h1_u8 *p=(const h1_u8 *)left,*q=(const h1_u8 *)right;
    while (n--) { int d=(int)*p++-(int)*q++;if (d) return d; }
    return 0;
}
