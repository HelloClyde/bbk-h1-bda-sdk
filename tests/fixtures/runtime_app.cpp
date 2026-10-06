#include "h1_v141.h"
static volatile int value;
static volatile unsigned order;
struct Initializer {
    unsigned digit;
    Initializer(unsigned d):digit(d) { order=order*10+d;value=41; }
    ~Initializer() { *(volatile unsigned *)0x80010000=(*(volatile unsigned *)0x80010000)*10+digit; }
};
static Initializer first __attribute__((init_priority(200)))(2);
static Initializer second __attribute__((init_priority(300)))(3);
static Initializer last(4);
extern "C" unsigned assembly_helper(unsigned);
extern "C" int h1_app_main(void)
{
    volatile unsigned long long a=1234567890123ULL,b=1001;
    if (order!=234) return -2;
    if (a/b!=1233334555ULL) return -1; /* actually links the 64-bit libgcc helper */
    return (int)assembly_helper((unsigned)value);
}
