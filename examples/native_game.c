/* Native V1.41 demo: system picker, simultaneous keys, touch exit,
 * direct framebuffer and optional continuous 1 kHz PCM tone. No game ROMs. */
#include "h1_v141.h"
#include "experimental/h1_pcm_stream.h"
static h1_pcm_stream audio;
static short ring[8192] __attribute__((aligned(32)));
static h1_game_input input;
int h1_app_main(void)
{
    char path[264];h1_game_framebuffer fb;h1_u32 tick,last=0,phase=0,progress_tick=0,progress_position=0;
    unsigned sound,i,x,y;short pcm[256];int event,key;
    if (h1_file_pick("A:\\","*",path,sizeof path)!=1) return 0;
    if (!h1_game_open()) return -1;
    if (!h1_game_framebuffer_get(&fb)) { h1_game_close();return -1; }
    sound=(unsigned)h1_pcm_stream_open(&audio,ring,8192,4096,32000);
    h1_game_input_reset(&input);
    for (;;) {
        /* Bound queue draining so an event storm cannot starve rendering. */
        for (i=0;i<128;++i) {
            h1_event_fetch(&event,&key);
            if (event==-1 && key==-1) break;
            if (event==H1_EVENT_TOUCH_DOWN ||
                (event==H1_EVENT_KEY_DOWN && (key==H1_KEY_BACK || key==H1_KEY_POWER))) goto done;
            h1_game_input_event(&input,event,(unsigned)key);
        }
        if (sound) {
            (void)h1_pcm_stream_poll(&audio);
            tick=h1_raw_tick_80hz();
            if (!audio.playing || audio.read_position!=progress_position) {
                progress_position=audio.read_position;progress_tick=tick;
            } else if ((h1_u32)(tick-progress_tick)>80u) {
                h1_pcm_stream_close(&audio);sound=0;
            }
            if (audio.queued<=audio.prefill) {
                for (i=0;i<256;++i) { pcm[i]=phase<16 ? 1500 : -1500;phase=(phase+1)%32; }
                (void)h1_pcm_stream_write(&audio,pcm,256);
            }
        }
        tick=h1_raw_tick_80hz();
        if (tick==last) continue;
        last=tick;h1_game_input_sample(&input);
        if (input.held[H1_KEY_ESCAPE]) break;
        for (y=0;y<fb.height;++y) for (x=0;x<fb.width;++x) {
            h1_u32 color=0x101020;
            if (x>=40 && x<160 && y>=80 && y<192) color=input.held[H1_KEY_D] ? 0x00ff00 : 0x404040;
            if (x>=240 && x<360 && y>=80 && y<192) color=input.held[H1_KEY_K] ? 0xff4000 : 0x404040;
            fb.pixels[y*(fb.stride_bytes/4)+x]=color;
        }
    }
done:
    h1_pcm_stream_close(&audio);h1_game_close();return 0;
}
