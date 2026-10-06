#ifndef H1_TCU_PROFILE_CLOCK_H
#define H1_TCU_PROFILE_CLOCK_H
#include "../h1_types.h"
/* Opt-in JZ4740 TCU5 borrowing, V1.41 only. Not a firmware clock API.
 * Explicit single owner, no CP0 Count, no global IRQ mask. Poll <1 second;
 * a missed complete 16-bit period cannot be recovered. One tick=1/32768 s.
 * Release before firmware/UI code that may acquire the channel. */
#ifndef H1_TCU_READ
static inline h1_u32 h1_tcu_read(unsigned offset,unsigned size)
{
    h1_u32 address=0xb0002000u+offset;
    if (size==1) return *(volatile h1_u8 *)address;
    if (size==2) return *(volatile h1_u16 *)address;
    return *(volatile h1_u32 *)address;
}
#define H1_TCU_READ h1_tcu_read
#endif
#ifndef H1_TCU_WRITE
static inline void h1_tcu_write(unsigned offset,h1_u32 value,unsigned size)
{
    h1_u32 address=0xb0002000u+offset;
    if (size==1) *(volatile h1_u8 *)address=(h1_u8)value;
    else if (size==2) *(volatile h1_u16 *)address=(h1_u16)value;
    else *(volatile h1_u32 *)address=value;
}
#define H1_TCU_WRITE h1_tcu_write
#endif
#define H1_TCU5_BIT 32u
#define H1_TCU5_FLAGS (32u | (32u<<16))
#define H1_TCU5_BASE 0x90u
typedef struct {
    unsigned owned,stopped,fault,retries;
    h1_u32 mask,total,previous;
    h1_u16 saved[4];
} h1_profile_clock;
static inline int h1_profile_clock_start(h1_profile_clock *s)
{
    unsigned i;
    if (!s) return 0;
    if (s->owned) return !s->fault;
    if ((H1_TCU_READ(0x10,1)&32u) || (H1_TCU_READ(0x20,4)&H1_TCU5_FLAGS)) return 0;
    s->stopped=(H1_TCU_READ(0x1c,4)&32u)!=0;
    if (s->stopped) H1_TCU_WRITE(0x3c,32,4);
    for (i=0;i<4;++i) s->saved[i]=(h1_u16)H1_TCU_READ(0x90+i*4,2);
    if (s->saved[3]&0x80u) {
        if (s->stopped) H1_TCU_WRITE(0x2c,32,4);
        return 0;
    }
    s->mask=H1_TCU_READ(0x30,4)&H1_TCU5_FLAGS;
    H1_TCU_WRITE(0x34,H1_TCU5_FLAGS,4);
    H1_TCU_WRITE(0x9c,0x200,2); /* input clock off for compare writes */
    H1_TCU_WRITE(0x90,65535,2);H1_TCU_WRITE(0x94,32767,2);
    H1_TCU_WRITE(0x9c,0x201,2); /* TCNT write requires PCLK / 1 */
    H1_TCU_WRITE(0x98,0,2);(void)H1_TCU_READ(0x98,2);
    H1_TCU_WRITE(0x9c,0x200,2);H1_TCU_WRITE(0x9c,0x202,2);
    H1_TCU_WRITE(0x28,H1_TCU5_FLAGS,4);
    s->total=s->previous=s->fault=s->retries=0;s->owned=1;
    H1_TCU_WRITE(0x14,32,1);return 1;
}
static inline int h1_profile_clock_sample(h1_profile_clock *s,h1_u32 *value)
{
    h1_u32 a=H1_TCU_READ(0x98,2),b,step;unsigned i;
    for (i=0;i<8;++i) {
        b=H1_TCU_READ(0x98,2);step=(b+65536u-a)%65536u;
        if (a<=65535 && b<=65535 && step<=1) { *value=b;return 1; }
        ++s->retries;a=b;
    }
    s->fault=1;return 0;
}
static inline int h1_profile_clock_read(h1_profile_clock *s,h1_u32 *ticks)
{
    h1_u32 value,full;
    if (!s || !ticks || !s->owned || s->fault || !h1_profile_clock_sample(s,&value)) return 0;
    full=H1_TCU_READ(0x20,4)&32u;
    if (value<s->previous && !full) {
        if (!h1_profile_clock_sample(s,&value)) return 0;
        full=H1_TCU_READ(0x20,4)&32u;
        if (value<s->previous && !full) {
            if (s->previous>=65471u && value<=64u) { *ticks=s->total;return 1; }
            s->fault=2;return 0;
        }
    }
    if (full && !h1_profile_clock_sample(s,&value)) return 0;
    /* Physical TCNT can remain at FULL while its flag is already set.
     * Clear only after the counter has actually reset, never at FULL. */
    if (value==65535 || value>=s->previous) full=0;
    if (full) H1_TCU_WRITE(0x28,32,4);
    if (value<s->previous && !full) { s->fault=2;return 0; }
    s->total+=full ? value+65536u-s->previous : value-s->previous;
    s->previous=value;*ticks=s->total;return 1;
}
static inline void h1_profile_clock_stop(h1_profile_clock *s)
{
    if (!s || !s->owned) return;
    H1_TCU_WRITE(0x18,32,1);H1_TCU_WRITE(0x9c,0x200,2);
    H1_TCU_WRITE(0x90,s->saved[0],2);H1_TCU_WRITE(0x94,s->saved[1],2);
    H1_TCU_WRITE(0x9c,0x201,2);H1_TCU_WRITE(0x98,s->saved[2],2);
    (void)H1_TCU_READ(0x98,2);H1_TCU_WRITE(0x9c,0x200,2);
    H1_TCU_WRITE(0x9c,s->saved[3],2);
    H1_TCU_WRITE(0x28,H1_TCU5_FLAGS,4);
    H1_TCU_WRITE(0x38,H1_TCU5_FLAGS&~s->mask,4);
    if (s->stopped) H1_TCU_WRITE(0x2c,32,4);
    s->owned=0;
}
#endif
