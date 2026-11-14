/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#ifndef H1NET_WIFI_CONFIG_H
#define H1NET_WIFI_CONFIG_H
#include "h1net_wire.h"
#define H1WIFI_AP_MAX 24u
#define H1WIFI_AP_BYTES 36u
#define H1WIFI_PAGE_MAX 12u
#define H1WIFI_STATUS_BYTES 110u
enum {H1WIFI_IDLE,H1WIFI_JOINING,H1WIFI_SAVED,H1WIFI_FAILED,H1WIFI_CANCELLED,H1WIFI_FORGETTING};
enum {H1WIFI_SCAN_IDLE,H1WIFI_SCANNING,H1WIFI_SCAN_DONE,H1WIFI_SCAN_FAILED};
enum {H1WIFI_ERR_NONE,H1WIFI_ERR_AUTH,H1WIFI_ERR_NO_AP,H1WIFI_ERR_TIMEOUT,
      H1WIFI_ERR_UNSUPPORTED,H1WIFI_ERR_STORAGE,H1WIFI_ERR_DRIVER};
typedef struct {char ssid[33];int rssi;unsigned auth,channel;} h1wifi_ap;
typedef struct {
    uint32_t operation,outcome,error,reason,connected,ip;
    int32_t rssi;
    uint32_t scan_id,scan_state,scan_count;
    char current[33],saved[33];
} h1wifi_status;
unsigned h1wifi_length(const char *,unsigned);
int h1wifi_auth_supported(unsigned);
int h1wifi_credentials_valid(const uint8_t *,unsigned,const uint8_t *,unsigned,unsigned,unsigned);
void h1wifi_status_encode(uint8_t out[H1WIFI_STATUS_BYTES],const h1wifi_status *);
int h1wifi_status_decode(h1wifi_status *,const uint8_t *,unsigned);
void h1wifi_ap_encode(uint8_t out[H1WIFI_AP_BYTES],const h1wifi_ap *);
int h1wifi_ap_decode(h1wifi_ap *,const uint8_t *,unsigned);
#endif
