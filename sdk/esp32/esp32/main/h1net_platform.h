/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#ifndef H1NET_PLATFORM_H
#define H1NET_PLATFORM_H
#include <stdbool.h>
#include "h1net_server.h"

void h1net_wifi_init(void);
bool h1net_wifi_connected(void);
void h1net_wifi_status(uint32_t out[6]);
uint16_t h1net_wifi_control(void *,uint8_t,const uint8_t *,unsigned,uint8_t *,unsigned *);
void h1net_config_task(void *argument);
void h1net_http_init(void);
void h1net_http_backend(h1net_backend *backend);
void h1net_http_disconnected(void);
unsigned h1net_http_get_diagnostics(h1net_info out[H1NET_MAX_JOBS]);
void h1net_usb_task(void *argument);
bool h1net_usb_connected(void);
enum {H1_BRIDGE_OPEN=1,H1_BRIDGE_CLOSE,H1_BRIDGE_DATA};
bool h1net_usb_bridge_submit(unsigned operation,uint32_t sequence,const uint8_t *data,unsigned length);
typedef struct {
    bool host_ready;
    unsigned devices;
    const char *stage;
    int last_error;
    bool phy_otg,host_mode,port_powered,port_attached;
    unsigned line_state;
    uint32_t raw_hprt,raw_wrap,raw_rtc;
    uint32_t rpc_frames,request_bytes,response_bytes,http_reads,tx_errors;
    uint32_t tx_wait_us_max,parser_bad_crc,parser_bad_headers;
    uint64_t tx_wait_us,dispatch_us;
} h1net_usb_diagnostics;
void h1net_usb_get_diagnostics(h1net_usb_diagnostics *out);
#endif
