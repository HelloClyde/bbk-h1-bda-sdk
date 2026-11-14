/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#ifndef H1NET_SERVER_H
#define H1NET_SERVER_H
#include "h1net_client.h"

typedef struct {
    void *context;
    uint16_t (*reset)(void *);
    void (*status)(void *,uint32_t out[6]);
    uint16_t (*open)(void *,const char *,uint32_t,uint32_t,uint32_t *);
    uint16_t (*info)(void *,uint32_t,h1net_info *);
    uint16_t (*read)(void *,uint32_t,unsigned,uint8_t *,uint32_t *,unsigned *);
    uint16_t (*cancel)(void *,uint32_t);
    uint16_t (*meta)(void *,uint32_t,char url[H1NET_MAX_URL+1],char type[97]);
    uint16_t (*range)(void *,const char *,const h1net_range *,uint32_t *);
    void (*capabilities)(void *,uint32_t out[4]);
    uint16_t (*release)(void *,uint32_t);
    uint16_t (*background)(void *,const char *,uint32_t,uint32_t,uint32_t *);
    int (*compute)(void *,uint8_t,const uint8_t *,unsigned,uint16_t *,uint8_t *,unsigned *);
    void (*compute_reset)(void *);
    /* Must deep-copy all input before returning; server erases staging immediately. */
    uint16_t (*request)(void *,const char *,const char *,const char *,const void *,unsigned,
                        uint32_t,uint32_t,uint32_t *);
    uint16_t (*wifi)(void *,uint8_t,const uint8_t *,unsigned,uint8_t *,unsigned *);
    /* Raw CRLF response fields, including duplicates, never silently truncated. */
    uint16_t (*headers)(void *,uint32_t,uint32_t,unsigned,uint8_t *,unsigned *,unsigned *);
} h1net_backend;
typedef struct {
    h1net_backend backend;
    uint16_t last_sequence;
    int ready;
    char range_url[H1NET_RANGE_URL+1];
    unsigned url_used,url_total;
    /* Keep servers off small task stacks (ESP USB server is static). */
    uint8_t request_data[H1NET_MAX_REQUEST];
    unsigned request_used,request_total;
} h1net_server;

void h1net_server_init(h1net_server *server,const h1net_backend *backend);
size_t h1net_server_dispatch(h1net_server *server,const h1net_frame *request,
                            uint8_t *response,size_t capacity);
#endif
