#include "h1_v141.h"
#include "experimental/h1_pcm_stream.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
_Static_assert(sizeof(h1_pcm_descriptor)==32,"PCM descriptor ABI");
_Static_assert(sizeof(h1_pcm_config)==36,"PCM configuration ABI");
static void *gui[0xb00/4],*sys[0x70/4];
static void *memory[0x18/4];
static h1_u8 allocation[128];
static unsigned allocation_calls,free_calls;
static unsigned keys[256],queries[256],picker_mode,picker_calls;
static unsigned opens,starts,stops,closes,destroys,inits,fail;
static h1_u32 node[6];
static h1_pcm_stream stream;
static short samples[32] __attribute__((aligned(32)));
static short input[64];
static int key_query(unsigned key) { ++queries[key];return keys[key]!=0; }
static void *allocate(h1_size_t size) { ++allocation_calls;return size<=sizeof allocation ? allocation : 0; }
static void release(void *p) { if (p==(void *)allocation) ++free_calls; }
static int game_open(void) { return 1; }
static int game_close(void) { return 1; }
static void *fb(h1_u16 *info,void *unused)
{
    (void)unused;info[1]=480;info[2]=272;info[3]=1920;info[5]=32;
    return (void *)0x81000000u;
}
static void picker(const char *dir,const char *filter,char *out)
{
    unsigned i;const char *p="A:\\GBA\\test.gba";(void)dir;(void)filter;++picker_calls;
    if (!picker_mode) { out[0]=0;return; }
    if (picker_mode==2) { for (i=0;i<300;++i) out[i]='a';out[300]=0;return; }
    for (i=0;p[i];++i) out[i]=p[i];
    out[i]=0;
}
static int descriptor_init(h1_pcm_descriptor *d)
{ ++inits;d->private_words[5]=0x12345678;return 1; }
static int descriptor_destroy(h1_pcm_descriptor *d)
{ if (d->private_words[5]!=0x12345678) fail=99;++destroys;return 1; }
static int device_open(h1_pcm_config *c)
{ ++opens;if (c->sample_rate!=32000 || c->mode!=1 || c->buffer_bytes!=4096) fail=99;return fail!=1; }
static int start(void) { ++starts;return fail!=2; }
static int stop(void) { ++stops;return 1; }
static int close_audio(void) { ++closes;return 1; }
static int submit(int route,h1_pcm_descriptor *d,int repeats,int flags)
{
    if (route || repeats || flags) fail=99;
    if (!d) { *(h1_u32 *)0x80007000=0;return 1; }
    node[1]=(h1_u32)d;node[2]=(h1_u32)d->pcm;
    *(h1_u32 *)0x80007000=(h1_u32)node;return 1;
}
__attribute__((section(".text.h1_bda_entry"),used)) int h1_bda_main(void)
{
    h1_game_input state;h1_game_framebuffer frame;h1_rtc_calendar calendar;char path[264];unsigned i;
    h1_u16 colors[9]={0xffff,0xf800,0x07e0,0x001f,0,0xffff,0,0,0};
    h1_u8 selected[45]={0};void *aligned;
    h1_u16 x=0,y=0;h1_u32 seconds=0;
    h1_u32 *code=(h1_u32 *)0x80008000,*touch=(h1_u32 *)0x80009000;
    *(void **)H1_RUNTIME_GUI_TABLE_SLOT=gui;*(void **)H1_RUNTIME_SYS_TABLE_SLOT=sys;
    *(void **)H1_RUNTIME_MEM_TABLE_SLOT=memory;
    memory[H1_MEM_ALLOC_OFFSET/4]=(void *)allocate;memory[H1_MEM_FREE_OFFSET/4]=(void *)release;
    CHECK(!h1_alloc_aligned(16,3) && !h1_alloc_aligned(0xffffffffu,32) && !allocation_calls);
    aligned=h1_alloc_aligned(32,32);CHECK(aligned && !((h1_u32)aligned&31));
    h1_free_aligned(aligned);CHECK(free_calls==1);CHECK(!h1_alloc_aligned(1024,32));
    gui[0x9d8/4]=(void *)key_query;gui[0x9ec/4]=(void *)picker;
    gui[0x84c/4]=(void *)game_open;gui[0x850/4]=(void *)game_close;gui[0x8f4/4]=(void *)fb;
    CHECK(!h1_runtime_entry(0,4));CHECK(!h1_runtime_entry(gui,1));
    CHECK(h1_game_open());CHECK(h1_game_framebuffer_get(&frame));
    CHECK(frame.stride_bytes==1920 && frame.pixels==(void *)0x81000000);
    CHECK(h1_game_blit_rgb565(&frame,colors,9,0,1,9,1));
    CHECK(frame.pixels[480]==0xffffff && frame.pixels[481]==0xff0000 &&
          frame.pixels[482]==0xff00 && frame.pixels[483]==0xff && !frame.pixels[488]);
    CHECK(!h1_game_blit_rgb565(&frame,colors,9,479,1,9,1));
    h1_game_close();
    CHECK(h1_game_scancode(H1_KEY_D)==32 && h1_game_scancode(H1_KEY_K)==37);
    h1_game_input_reset(&state);keys[32]=keys[37]=1;h1_game_input_sample(&state);
    CHECK(state.held[H1_KEY_D] && state.held[H1_KEY_K]);keys[32]=0;
    h1_game_input_sample(&state);CHECK(!state.held[H1_KEY_D] && state.held[H1_KEY_K]);
    keys[28]=1;h1_game_input_event(&state,H1_EVENT_KEY_DOWN,H1_KEY_CONFIRM);
    queries[28]=0;h1_game_input_sample(&state);
    CHECK(state.held[H1_KEY_CONFIRM] && !state.held[H1_KEY_ENTER] && queries[28]==1);
    for (i=0;i<256;++i) queries[i]=0;
    selected[H1_KEY_D]=selected[H1_KEY_K]=1;h1_game_input_sample_selected(&state,selected);
    CHECK(queries[32]==1 && queries[37]==1 && !queries[28] && !queries[16]);
    CHECK(!h1_game_scancode(43));CHECK(h1_file_pick("A:\\","",path,sizeof path)==-1);
    CHECK(!picker_calls);CHECK(h1_file_pick("A:\\","gba;gb;gbc",path,sizeof path)==0);
    picker_mode=1;CHECK(h1_file_pick("A:\\","gba",path,sizeof path)==1 && path[3]=='G');
    CHECK(h1_file_pick("A:\\","gba",path,4)==-1 && !path[0]);
    picker_mode=2;CHECK(h1_file_pick("A:\\","*",path,sizeof path)==-1);
    CHECK(h1_path_directory_gbk("A:\\\x81\x5c.gba",path,sizeof path) && path[3]==0);
    CHECK(!h1_touch_position(&x,&y));
    touch[0]=0x27bdffa8;touch[1]=0xafbf0050;touch[2]=0xafb60048;touch[3]=0xafb7004c;
    touch[4]=0x00a0b821;touch[5]=0x0080b021;touch[6]=0x2402002a;touch[7]=0xa6c20000;
    touch[8]=0x24020018;touch[9]=0xa6e20000;touch[10]=0x8fb60048;touch[11]=0x8fb7004c;
    touch[12]=0x8fbf0050;touch[13]=0x03e00008;touch[14]=0x27bd0058;
    gui[0x6c0/4]=touch;CHECK(h1_touch_position(&x,&y) && x==42 && y==24);
    touch[8]=0x24020110;CHECK(!h1_touch_position(&x,&y) && y==24);
    CHECK(h1_rtc_read_local_seconds(&seconds) && seconds==1791199800u);
    CHECK(h1_rtc_calendar_from_seconds(951782400u,&calendar) && calendar.year==2000 && calendar.month==2 && calendar.day==29);
    CHECK(!h1_rtc_calendar_from_seconds(4102444800u,&calendar));
    *(h1_u32 *)0xb0003000=0;CHECK(!h1_rtc_read_local_seconds(&seconds));
    sys[0x50/4]=(void *)descriptor_init;sys[0x54/4]=(void *)descriptor_destroy;
    sys[0x58/4]=(void *)device_open;sys[0x60/4]=(void *)start;
    sys[0x64/4]=(void *)stop;sys[0x68/4]=(void *)close_audio;sys[0x5c/4]=code;
    code[0]=0x08000000u|(((h1_u32)submit>>2)&0x03ffffffu);code[1]=0;
    CHECK(!h1_pcm_stream_open(&stream,samples,32,16,32000));CHECK(!inits);
    code[0x70/4]=0x3c068000;code[0x74/4]=0x24c67000;code[0x78/4]=0x00c43021;
    CHECK(h1_pcm_stream_open(&stream,samples,32,16,32000));
    for (i=0;i<64;++i) input[i]=(short)(i+1);
    CHECK(h1_pcm_stream_write(&stream,input,16)==16 && stream.playing && stream.queued==16);
    node[2]+=8;CHECK(h1_pcm_stream_poll(&stream)==1 && stream.queued==12);
    CHECK(!samples[0] && samples[4]==5);CHECK(h1_pcm_stream_write(&stream,input,30)==20);
    CHECK(stream.queued==32);node[2]=(h1_u32)samples+4;
    CHECK(h1_pcm_stream_poll(&stream)==1 && stream.queued==2);
    h1_pcm_stream_pause(&stream);
    CHECK(stream.paused && !stream.queued && !closes && inits==1);
    CHECK(h1_pcm_stream_resume(&stream) && opens==1 && inits==1);
    CHECK(h1_pcm_stream_write(&stream,input,16)==16);node[2]+=40;
    CHECK(h1_pcm_stream_poll(&stream)==-1 && stream.underruns==1 && !stream.queued);
    h1_pcm_stream_close(&stream);CHECK(closes==1 && destroys==1 && !fail);
    fail=1;CHECK(!h1_pcm_stream_open(&stream,samples,32,16,32000));CHECK(destroys==2);
    fail=2;CHECK(!h1_pcm_stream_open(&stream,samples,32,16,32000));CHECK(destroys==3 && closes==2);
    return 0;
}
