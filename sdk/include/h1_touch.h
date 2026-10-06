#ifndef H1_TOUCH_H
#define H1_TOUCH_H
#include "h1_runtime.h"
/* V1.41 calibrated LCD coordinates, not the stale native-game cache.
 * Fail closed on an unrecognized implementation. Output only on success. */
static inline int h1_touch_position(h1_u16 *x, h1_u16 *y)
{
    typedef void (*fn_type)(h1_u16 *,h1_u16 *);
    const h1_u32 *code=(const h1_u32 *)h1_runtime_entry(
        h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x6C0u);
    h1_u16 px=65535,py=65535;
    if (!x || !y || ((h1_u32)code&3u) || (h1_u32)code<0x80000000u ||
        (h1_u32)code>0x83ffffe8u || code[0]!=0x27bdffa8u || code[1]!=0xafbf0050u ||
        code[4]!=0x00a0b821u || code[5]!=0x0080b021u) return 0;
    ((fn_type)code)(&px,&py);
    if (px>=480 || py>=272) return 0;
    *x=px;*y=py;return 1;
}
#endif
