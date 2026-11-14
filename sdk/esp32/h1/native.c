/* SPDX-License-Identifier: Apache-2.0 */
#include "h1_esp32.h"
#include "usb.h"
#include "firmware.h"
static debug_usb usb;
static h1_esp32 *owner;
static uint32_t bound,frame_bytes,raw_tick,ms,half_ms;
static int zlp;
static void (*user_idle)(void *);
static void *user_context;
static uint32_t clock_ms(void *unused)
{
    (void)unused;uint32_t tick=fw_tick(),delta=tick-raw_tick;raw_tick=tick;
    /* 80 Hz: retain the half millisecond and unsigned tick wrap. */
    ms+=(delta/2u)*25u;half_ms+=(delta&1u)*25u;
    ms+=half_ms/2u;half_ms&=1u;return ms;
}
static void poll(void *unused){(void)unused;debug_usb_poll(&usb);}
static int connected(void *unused)
{(void)unused;return usb.owned&&!usb.ownership_fault&&usb.configured&&(usb.control_lines&1u);}
static uint32_t epoch(void *unused){(void)unused;return usb.session_epoch;}
static void bind(void *unused)
{
    (void)unused;bound=usb.session_epoch;frame_bytes=0;zlp=0;
    uint8_t stale[64];for(unsigned i=0;i<64&&debug_usb_read(&usb,stale,sizeof stale)>0;i++){}
}
static int live(void){return connected(0)&&bound==usb.session_epoch;}
static int write_bytes(void *unused,const uint8_t *p,unsigned n)
{
    poll(unused);if(!live())return -1;if(n>64)n=64;
    if(!debug_usb_write(&usb,p,n))return 0;frame_bytes+=n;return (int)n;
}
static int read_bytes(void *unused,uint8_t *p,unsigned n)
{poll(unused);return live()?debug_usb_read(&usb,p,n):-1;}
static int finish(void *unused)
{
    poll(unused);if(!live())return -1;if(!debug_usb_tx_idle(&usb))return 0;
    if(frame_bytes&&!(frame_bytes%64)&&!zlp){
        if(!debug_usb_write(&usb,0,0))return 0;zlp=1;return 0;
    }
    frame_bytes=0;zlp=0;return 1;
}
static void idle(void *unused){poll(unused);if(user_idle)user_idle(user_context);}
static void close_native(void *unused)
{(void)unused;debug_usb_close(&usb);owner=0;user_idle=0;user_context=0;}
int h1_esp32_open(h1_esp32 *e,void (*pump)(void *),void *context)
{
    if(!e)return H1_ESP32_ARGUMENT;
    if(owner)return H1_ESP32_OWNER_BUSY;
    if(!fw_matches())return H1_ESP32_FIRMWARE;
    rd_header *resident=fw_task_control();
    if(!resident&&!fw_task_slot_empty())return H1_ESP32_OWNER_BUSY;
    /* Recognized resident only: request stop, never delete its task or RAM. */
    if(resident&&resident->state!=RD_STOPPED){
        resident->stop_request=1;uint32_t start=fw_tick();
        for(unsigned i=0;i<160&&fw_tick()-start<160u;i++){
            resident=fw_task_control();if(!resident||resident->state==RD_STOPPED)break;
            if(pump)pump(context);fw_delay(1);
        }
        resident=fw_task_control();
        if(resident&&resident->state!=RD_STOPPED)return H1_ESP32_OWNER_BUSY;
        if(!resident&&!fw_task_slot_empty())return H1_ESP32_OWNER_BUSY;
    }
    int r=debug_usb_open_attached(&usb);
    if(r)return r==1?H1_ESP32_FIRMWARE:H1_ESP32_OWNER_BUSY;
    raw_tick=fw_tick();ms=half_ms=0;user_idle=pump;user_context=context;
    h1_esp32_driver d={{0,write_bytes,read_bytes,finish,idle,clock_ms},poll,connected,epoch,bind,close_native};
    r=h1_esp32_init(e,&d);if(r){close_native(0);return r;}
    owner=e;e->native=1;return 0;
}
