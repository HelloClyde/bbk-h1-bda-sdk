/* Reuse the existing MIT-licensed H1 PCM ring, including volume restoration. */
#include <stdint.h>
static uint64_t clock_samples;
static unsigned clock_valid,clock_observing;
static uint32_t clock_tick;
static void clock_opened(void);
static void clock_closed(void);
static void clock_started(void);
static void clock_observe(void);
static void clock_played(unsigned);
static void clock_lost(void);
static void clock_before_pause(void);
#define H1_AUDIO_CLOCK_OPENED() clock_opened()
#define H1_AUDIO_CLOCK_CLOSED() clock_closed()
#define H1_AUDIO_CLOCK_STARTED() clock_started()
#define H1_AUDIO_CLOCK_OBSERVE() clock_observe()
#define H1_AUDIO_CLOCK_PLAYED(n) clock_played(n)
#define H1_AUDIO_CLOCK_LOST() clock_lost()
#define H1_AUDIO_CLOCK_BEFORE_PAUSE() clock_before_pause()
#define H1_DIAGNOSTICS_H
#define H1_PROFILE_H
#define H1_PROFILE 0
#define h1_diag(...) ((void)0)
#define h1_diag_is_verbose() 0
#include "pcm.c"

/* The only time base is retired, valid 32000Hz PCM, not submitted samples or
 * elapsed wall time. The native cursor is modulo8192: a full unnoticed loop
 * is indistinguishable from no movement. An 80Hz timer is used ONLY to refuse
 * that ambiguous observation, never to manufacture a playback position.
 * At delta20 ticks the true gap may reach262.5ms due to tick quantization,
 * exceeding the256ms ring; delta<=19 bounds it below one full ring. */
static void clock_opened(void){clock_samples=0;clock_valid=1;clock_observing=0;}
static void clock_closed(void){clock_valid=clock_observing=0;}
static void clock_started(void){clock_tick=h1_raw_tick_80hz();clock_observing=1;}
static void clock_observe(void)
{
    uint32_t tick=h1_raw_tick_80hz();
    if(clock_observing && (uint32_t)(tick-clock_tick)>=20u)clock_valid=0;
    clock_tick=tick;clock_observing=1;
}
static void clock_played(unsigned count)
{
    if(UINT64_MAX-clock_samples<count){clock_samples=UINT64_MAX;clock_valid=0;}
    else clock_samples+=count;
}
static void clock_lost(void){clock_valid=0;clock_observing=0;}
/* The shared hook runs after native stop, before cancellation clears cursor. */
static void clock_before_pause(void){retire();clock_observing=0;}

/* media_ms wraps modulo2^32 milliseconds; use wrap-safe time differences.
 * The uint64 sample accumulator itself survives the32-bit sample boundary.
 * When return0, outputs are diagnostics only and must not drive video sync.
 * Startup/prefill is unavailable until at least one valid sample is retired.
 * Paused clocks with prior playback remain available and frozen; unread/discarded samples never
 * advance media_ms. Underrun, driver failure or an ambiguous cursor gap is
 * sticky-unavailable until explicit reset or successful device reopen. */
int h1ns_audio_clock(uint32_t *media_ms,unsigned *buffered_ms)
{
    if(!media_ms||!buffered_ms)return 0;
    if(active&&!paused)retire();
    *media_ms=(uint32_t)(clock_samples/(OUTPUT_RATE/1000u));
    *buffered_ms=active?queued/(OUTPUT_RATE/1000u):0;
    return active&&clock_valid&&clock_samples!=0;
}
/* For seek/new media, first pause/discard old PCM, then reset the origin.
 * Reset alone preserves the audio path and already-queued valid samples. */
void h1ns_audio_clock_reset(void)
{
    if(active&&!paused)retire();
    clock_samples=0;clock_valid=active;clock_observing=playing&&!paused;
    clock_tick=h1_raw_tick_80hz();
}
unsigned h1ns_audio_pending(void){if(!active || paused)return 0;retire();return queued;}
int h1ns_audio_source_rate(unsigned source_rate)
{
    if(!active)return gba_audio_open(source_rate);
    if(rate==source_rate)return 1;
    /* HE-AAC can reveal its doubled output rate in the first packet. The
       native device stays at OUTPUT_RATE: only reset the software resampler,
       avoiding another slow close/open of all native PCM descriptors. */
    gba_audio_pause();rate=source_rate;return gba_audio_resume();
}
int h1ns_audio_video_wait(void)
{
    if(!active||paused)return 0;
    retire();
    /* AAC supplies at most 2048 source samples (HE-AAC), or 1024 at the
       lowest LC rates. Reserve a complete resampled packet, while letting
       the video decoder use more than the emulator's fixed 128ms watermark. */
    unsigned reserve=(2048u*OUTPUT_RATE+rate-1)/rate;if(reserve>4096)reserve=4096;
    if(!playing||queued<=BLOCK_SAMPLES-reserve){waiting=0;return 0;}
    return gba_audio_wait();
}
volatile uint32_t h1ns_audio_state[8];
volatile uint32_t h1ns_audio_format[2];
void h1ns_audio_snapshot(void){h1ns_audio_format[0]=rate;h1ns_audio_format[1]=OUTPUT_RATE;h1ns_audio_state[0]=active;h1ns_audio_state[1]=playing;h1ns_audio_state[2]=queued;h1ns_audio_state[3]=submitted;h1ns_audio_state[4]=nonzero;h1ns_audio_state[5]=dropped;h1ns_audio_state[6]=failures;h1ns_audio_state[7]=underruns;}
