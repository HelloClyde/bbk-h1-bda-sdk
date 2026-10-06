#include "h1_types.h"
static h1_u32 model_read(unsigned,unsigned);
static void model_write(unsigned,h1_u32,unsigned);
#define H1_TCU_READ model_read
#define H1_TCU_WRITE model_write
#include "experimental/h1_profile_clock.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static h1_u32 regs[64],original[64];
static unsigned invalid,writes,unstable;
static h1_u32 model_read(unsigned offset,unsigned size)
{
    (void)size;
    if (offset>=0x90 && offset<0xa0 && (regs[0x1c/4]&32)) invalid=1;
    if (offset==0x98 && unstable) { ++regs[offset/4];return regs[offset/4]*10; }
    return regs[offset/4];
}
static void model_write(unsigned offset,h1_u32 value,unsigned size)
{
    ++writes;
    if (offset>=0x90 && offset<0xa0) {
        if (size!=2 || (regs[0x1c/4]&32)) invalid=1;
        if ((offset==0x90 || offset==0x94) && (regs[0x9c/4]&7)) invalid=2;
        if (offset==0x98 && (regs[0x9c/4]&0x3f)!=1) invalid=3;
        regs[offset/4]=value;return;
    }
    if (value&~H1_TCU5_FLAGS) invalid=4;
    switch (offset) {
    case 0x14:regs[0x10/4]|=value;break;
    case 0x18:regs[0x10/4]&=~value;break;
    case 0x28:regs[0x20/4]&=~value;break;
    case 0x2c:regs[0x1c/4]|=value;break;
    case 0x3c:regs[0x1c/4]&=~value;break;
    case 0x34:regs[0x30/4]|=value;break;
    case 0x38:regs[0x30/4]&=~value;break;
    default:invalid=5;
    }
}
__attribute__((section(".text.h1_bda_entry"),used)) int h1_bda_main(void)
{
    h1_profile_clock c={0};h1_u32 ticks;unsigned i;
    regs[0x10/4]=7;regs[0x1c/4]=0xfff8;regs[0x30/4]=0xa5a55a5a;
    regs[0x90/4]=4000;regs[0x94/4]=2000;regs[0x98/4]=123;regs[0x9c/4]=4;
    for (i=0;i<64;++i) original[i]=regs[i];
    CHECK(h1_profile_clock_start(&c) && c.owned && !invalid);
    regs[0x98/4]=65534;CHECK(h1_profile_clock_read(&c,&ticks) && ticks==65534);
    regs[0x20/4]|=32;regs[0x98/4]=65535;
    CHECK(h1_profile_clock_read(&c,&ticks) && ticks==65535 && (regs[0x20/4]&32));
    regs[0x98/4]=0;CHECK(h1_profile_clock_read(&c,&ticks) && ticks==65536 && !(regs[0x20/4]&32));
    regs[0x98/4]=10;CHECK(h1_profile_clock_read(&c,&ticks) && ticks==65546);
    regs[0x98/4]=8;CHECK(!h1_profile_clock_read(&c,&ticks) && c.fault==2);
    h1_profile_clock_stop(&c);CHECK(!c.owned && !invalid);
    for (i=0;i<64;++i) CHECK(regs[i]==original[i]);
    CHECK(h1_profile_clock_start(&c));unstable=1;
    CHECK(!h1_profile_clock_read(&c,&ticks) && c.fault==1 && c.retries==8);
    unstable=0;h1_profile_clock_stop(&c);
    regs[0x10/4]|=32;writes=0;CHECK(!h1_profile_clock_start(&c) && !writes);
    regs[0x10/4]&=~32u;regs[0x9c/4]=0x80;
    CHECK(!h1_profile_clock_start(&c) && !c.owned && !invalid);
    return 0;
}
