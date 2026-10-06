#include "h1_v141.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static h1_u32 code[8] __attribute__((aligned(16)));
__attribute__((section(".text.h1_bda_entry"),used)) int h1_bda_main(void)
{
    int (*generated)(void)=(int (*)(void))code;
    CHECK(!h1_code_cache_sync((void *)0x10000000,(void *)0x10000040));
    CHECK(!h1_code_cache_sync((void *)0x83fffff0,(void *)0x84000001));
    CHECK(h1_code_cache_sync(code,code));
    code[0]=0x03e00008;code[1]=0x24020029;
    CHECK(h1_code_cache_sync(code,code+2) && generated()==41);
    code[1]=0x2402002a;
    CHECK(h1_code_cache_sync(code,code+2) && generated()==42);
    return 0;
}
