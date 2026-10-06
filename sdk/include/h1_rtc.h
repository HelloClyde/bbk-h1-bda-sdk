#ifndef H1_RTC_H
#define H1_RTC_H
#include "h1_types.h"
/* JZ4740 RTC, V1.41 calendar convention: seconds since 1970, local civil
 * time. Do NOT add the timezone again. No clock/control/alarm writes. */
static inline int h1_rtc_read_local_seconds(h1_u32 *out)
{
    volatile const h1_u32 *control=(volatile const h1_u32 *)0xB0003000u;
    volatile const h1_u32 *seconds=(volatile const h1_u32 *)0xB0003004u;
    unsigned attempt;h1_u32 a,b;
    if (!out || !(*control&1u)) return 0;
    for (attempt=0;attempt<8;++attempt) {
        a=*seconds;b=*seconds;
        if (a==b && a>=946684800u && a<4102444800u && (*control&1u)) {
            *out=a;return 1;
        }
    }
    return 0;
}
typedef struct {
    h1_u16 year;
    h1_u8 month,day,hour,minute,second,weekday; /* Sunday = 0 */
} h1_rtc_calendar;
static inline int h1_rtc_calendar_from_seconds(h1_u32 seconds,h1_rtc_calendar *out)
{
    static const h1_u8 month_days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    h1_u32 days=seconds/86400u,remaining=days,year=1970,month=0,length;
    if (!out || seconds<946684800u || seconds>=4102444800u) return 0;
    for (;;) {
        length=365u+(year%4==0 && (year%100!=0 || year%400==0));
        if (remaining<length) break;
        remaining-=length;++year;
    }
    for (;;) {
        length=month_days[month]+(month==1 && year%4==0 && (year%100!=0 || year%400==0));
        if (remaining<length) break;
        remaining-=length;++month;
    }
    out->year=(h1_u16)year;out->month=(h1_u8)(month+1);out->day=(h1_u8)(remaining+1);
    out->weekday=(h1_u8)((days+4)%7);out->hour=(h1_u8)((seconds%86400u)/3600u);
    out->minute=(h1_u8)((seconds%3600u)/60u);out->second=(h1_u8)(seconds%60u);return 1;
}
static inline int h1_rtc_read_calendar(h1_rtc_calendar *out)
{
    h1_u32 seconds;
    return out && h1_rtc_read_local_seconds(&seconds) && h1_rtc_calendar_from_seconds(seconds,out);
}
#endif
