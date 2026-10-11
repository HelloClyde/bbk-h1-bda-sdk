#include "h1_v141.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static void *sys[0x70/4];
static unsigned calls;
/* Return a deliberately meaningless value: SYS+0x40 has a void ABI. */
static int set_volume(int percent)
{
    ++calls;*(volatile int *)0x8000b000u=percent;return -123;
}
__attribute__((section(".text.h1_bda_entry"),used)) int h1_bda_main(void)
{
    h1_u32 *set=(h1_u32 *)0x80008000u,*get=(h1_u32 *)0x80009000u;
    int original=-1,value=1234;
    *(void **)H1_RUNTIME_SYS_TABLE_SLOT=0;
    CHECK(!h1_audio_get_volume(&value) && value==1234);
    CHECK(!h1_audio_set_volume(50) && !calls);
    *(void **)H1_RUNTIME_SYS_TABLE_SLOT=sys;
    CHECK(!h1_audio_get_volume(0));
    CHECK(!h1_audio_set_volume(50) && !calls);
    /* Executable MIPS stubs with the verified V1.41 leading instructions. */
    set[0]=0x28850065u;set[1]=0x24030064u;
    set[2]=0x08000000u|(((h1_u32)set_volume>>2)&0x03ffffffu);set[3]=0;
    get[0]=0x3c028001u;get[1]=0x03e00008u;get[2]=0x8c42b000u;
    sys[0x40/4]=set;sys[0x44/4]=get;
    *(volatile int *)0x8000b000u=51;
    CHECK(h1_audio_get_volume(&original) && original==51 && !calls);
    CHECK(h1_audio_set_volume(70) && calls==1);
    CHECK(h1_audio_get_volume(&value) && value==70);
    CHECK(h1_audio_set_volume(-2147483647-1));
    CHECK(h1_audio_get_volume(&value) && value==0);
    CHECK(h1_audio_set_volume(2147483647));
    CHECK(h1_audio_get_volume(&value) && value==100);
    CHECK(h1_audio_set_volume(0));
    CHECK(h1_audio_get_volume(&value) && value==0);
    CHECK(h1_audio_set_volume(100));
    CHECK(h1_audio_get_volume(&value) && value==100);
    CHECK(h1_audio_set_volume(original));
    CHECK(h1_audio_get_volume(&value) && value==51 && calls==6);
    *(volatile int *)0x8000b000u=-1;
    CHECK(!h1_audio_get_volume(&value) && value==51);
    *(volatile int *)0x8000b000u=101;
    CHECK(!h1_audio_get_volume(&value) && value==51);
    set[0]^=1;
    CHECK(!h1_audio_set_volume(20) && calls==6);
    CHECK(!h1_audio_get_volume(&value) && value==51);set[0]^=1;
    set[1]^=1;
    CHECK(!h1_audio_set_volume(20) && calls==6);set[1]^=1;
    get[0]^=0x10000;
    CHECK(!h1_audio_get_volume(&value) && value==51);get[0]^=0x10000;
    get[1]^=1;
    CHECK(!h1_audio_set_volume(20) && calls==6);get[1]^=1;
    get[2]^=0x10000;
    CHECK(!h1_audio_get_volume(&value) && value==51);get[2]^=0x10000;
    sys[0x40/4]=0;
    CHECK(!h1_audio_set_volume(20) && calls==6);sys[0x40/4]=set;
    sys[0x44/4]=0;
    CHECK(!h1_audio_get_volume(&value) && value==51);sys[0x44/4]=get;
    sys[0x40/4]=(void *)0x80008001u;
    CHECK(!h1_audio_set_volume(20) && calls==6);sys[0x40/4]=set;
    sys[0x44/4]=(void *)0x80009001u;
    CHECK(!h1_audio_get_volume(&value) && value==51);sys[0x44/4]=get;
    sys[0x40/4]=(void *)0x7ffffffcu;
    CHECK(!h1_audio_set_volume(20) && calls==6);
    sys[0x40/4]=(void *)0x83fffff8u;
    CHECK(!h1_audio_set_volume(20) && calls==6);sys[0x40/4]=set;
    sys[0x44/4]=(void *)0x7ffffffcu;
    CHECK(!h1_audio_get_volume(&value) && value==51);
    sys[0x44/4]=(void *)0x83fffff8u;
    CHECK(!h1_audio_get_volume(&value) && value==51);
    CHECK(!h1_audio_volume_entry(0x50u));
    return 0;
}
