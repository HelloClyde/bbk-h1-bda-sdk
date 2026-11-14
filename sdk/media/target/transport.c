#include "transport.h"
#ifdef H1NET_DEBUG_TRANSPORT
/* Model-only RPC mailbox. No hardware USB is accessed by this build.
 * The model verifies loaded bytes, build ID, magic and schema before writing. */
typedef struct {
    uint32_t header[16];
    uint8_t tx[H1NET_MAX_FRAME],rx[H1NET_MAX_FRAME];
} debug_mailbox;
typedef char check_mailbox_size[(sizeof(debug_mailbox)==1120)?1:-1];
volatile debug_mailbox h1net_debug_mailbox;
static uint32_t request_used;
#define BOX h1net_debug_mailbox
#define BARRIER() __asm__ volatile("":::"memory")
int h1net_transport_open(void)
{
    BOX.header[0]=0x31445748u;BOX.header[1]=1;BOX.header[2]=sizeof BOX;
    BOX.header[3]=H1NET_DBG_BUILD_ID;BOX.header[4]=1;return 0;
}
void h1net_transport_close(void){BOX.header[10]=0;BOX.header[4]=0;}
int h1net_transport_open_attached(void){return h1net_transport_open();}
void h1net_transport_poll(void){}
int h1net_transport_needs_unplug(void){return 0;}
int h1net_transport_faulted(void){return 0;}
int h1net_transport_connected(void){return BOX.header[4] && BOX.header[10]!=0;}
uint32_t h1net_transport_epoch(void){return BOX.header[10];}
void h1net_transport_bind(void){request_used=0;BOX.header[5]=BOX.header[7]=0;}
int h1net_transport_write(void *unused,const uint8_t *data,unsigned n)
{
    (void)unused;if(!h1net_transport_connected())return -1;
    if(BOX.header[5])return 0;
    if(n>H1NET_MAX_FRAME-request_used)return -1;
    for(unsigned i=0;i<n;++i)BOX.tx[request_used+i]=data[i];request_used+=n;return (int)n;
}
int h1net_transport_finish(void *unused)
{
    (void)unused;if(!h1net_transport_connected())return -1;
    if(!request_used || BOX.header[5])return -1;
    BOX.header[6]=request_used;request_used=0;BARRIER();BOX.header[5]=1;return 1;
}
int h1net_transport_read(void *unused,uint8_t *data,unsigned n)
{
    (void)unused;if(!h1net_transport_connected())return -1;if(!BOX.header[7])return 0;
    uint32_t length=BOX.header[8],offset=BOX.header[9];
    if(length>H1NET_MAX_FRAME || offset>length)return -1;
    if(n>length-offset)n=length-offset;
    for(unsigned i=0;i<n;++i)data[i]=BOX.rx[offset+i];
    BOX.header[9]=offset+n;BARRIER();if(offset+n==length)BOX.header[7]=0;return (int)n;
}
#else
#include "usb.h"
static debug_usb usb;
static uint32_t frame_bytes,bound_epoch;
static int zlp_sent;
int h1net_transport_open(void){return debug_usb_open(&usb);}
int h1net_transport_open_attached(void){return debug_usb_open_attached(&usb);}
void h1net_transport_close(void){debug_usb_close(&usb);}
void h1net_transport_poll(void){debug_usb_poll(&usb);}
int h1net_transport_needs_unplug(void){return debug_usb_cable_connected();}
int h1net_transport_faulted(void){return usb.owned && usb.ownership_fault;}
int h1net_transport_connected(void)
{return !usb.ownership_fault && usb.configured && (usb.control_lines&1u);}
uint32_t h1net_transport_epoch(void){return usb.session_epoch;}
void h1net_transport_bind(void){bound_epoch=usb.session_epoch;frame_bytes=0;zlp_sent=0;}
static int live(void){return h1net_transport_connected() && usb.session_epoch==bound_epoch;}
int h1net_transport_write(void *unused,const uint8_t *data,unsigned n)
{
    (void)unused;debug_usb_poll(&usb);if(!live())return -1;if(n>64)n=64;
    if(!debug_usb_write(&usb,data,n))return 0;frame_bytes+=n;return (int)n;
}
int h1net_transport_read(void *unused,uint8_t *data,unsigned n)
{(void)unused;debug_usb_poll(&usb);return live()?debug_usb_read(&usb,data,n):-1;}
int h1net_transport_finish(void *unused)
{
    (void)unused;debug_usb_poll(&usb);if(!live())return -1;
    if(!debug_usb_tx_idle(&usb))return 0;
    if(frame_bytes && !(frame_bytes%64) && !zlp_sent){
        if(!debug_usb_write(&usb,NULL,0))return 0;zlp_sent=1;return 0;
    }
    frame_bytes=0;zlp_sent=0;return 1;
}
#endif
