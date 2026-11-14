/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#ifndef H1NET_CLIENT_H
#define H1NET_CLIENT_H
#include "h1net_wire.h"

enum h1net_client_error {
    H1NET_IO=-1, H1NET_TIMEOUT=-2, H1NET_PROTOCOL=-3,
    H1NET_REMOTE=-4, H1NET_SINK=-5, H1NET_ABORTED=-6
};
typedef struct {
    void *context;
    /* Nonblocking byte transport. Return accepted/read bytes, 0 to wait, -1 on failure. */
    int (*write)(void *,const uint8_t *,unsigned);
    int (*read)(void *,uint8_t *,unsigned);
    /* Complete a frame, including USB ZLP if required: 1 done, 0 wait, -1 failure. */
    int (*finish_write)(void *);
    void (*idle)(void *);
    uint32_t (*now_ms)(void *);
} h1net_io;
typedef struct {
    h1net_io io;
    h1net_parser parser;
    uint16_t sequence, expected_sequence, remote_status;
    uint8_t expected_opcode;
    uint8_t response[H1NET_MAX_PAYLOAD];
    unsigned response_length;
    int ready, broken, response_ready, response_invalid, pending_rpc;
    uint32_t rpc_timeout_ms;
    uint32_t max_jobs,features;
} h1net_client;

/* A caller-owned cooperative RPC. Only one RPC may own a client at a time.
 * poll performs bounded transport work, never waits or invokes io.idle.
 * The validated reply stays in client.response until the next begin. */
typedef struct {
    uint8_t packet[H1NET_MAX_FRAME];
    unsigned size,offset,phase;
    uint32_t start,bad_headers,bad_crc;
} h1net_rpc_task;
int h1net_rpc_begin(h1net_client *,h1net_rpc_task *,uint8_t,const void *,unsigned);
/* 0 pending, 1 complete, negative h1net_client_error on failure. */
int h1net_rpc_poll(h1net_client *,h1net_rpc_task *);
typedef struct {
    uint32_t job, state, http_status, content_length;
    uint32_t received, delivered, buffered, error;
} h1net_info;
typedef struct {
    uint32_t offset,length,total,timeout_ms;
    const char *referer;
} h1net_range;
typedef int (*h1net_sink_fn)(void *context,const uint8_t *data,unsigned length);
/* Return nonzero to cancel. Called only between complete RPCs. */
typedef int (*h1net_progress_fn)(void *context,const h1net_info *info);

void h1net_client_init(h1net_client *client,const h1net_io *io,uint32_t rpc_timeout_ms);
int h1net_rpc(h1net_client *client,uint8_t opcode,const void *payload,unsigned length,
             void *response,unsigned capacity,unsigned *received);
int h1net_hello(h1net_client *client);
int h1net_get_status(h1net_client *client,uint32_t status[6]);
int h1net_capabilities(h1net_client *client);
int h1net_http_release(h1net_client *client,uint32_t job);
int h1net_http_open(h1net_client *client,const char *url,uint32_t max_bytes,
                   uint32_t timeout_ms,uint32_t *job);
/* Requires CAP_HTTP_REQUEST. GET/POST only; credentials and bodies are never
 * redirected. Accepted jobs use the ordinary INFO/READ/CANCEL/RELEASE APIs. */
int h1net_http_request_open(h1net_client *client,const char *url,const char *method,
                          const char *headers,const void *body,unsigned body_length,
                          uint32_t max_bytes,uint32_t timeout_ms,uint32_t *job);
/* Open a validated byte range without draining it. On failure *job is zero;
 * the caller owns READ/INFO and cancellation or release of an accepted job. */
int h1net_http_range_open(h1net_client *client,const char *url,const h1net_range *range,
                         uint32_t *job);
int h1net_http_info(h1net_client *client,uint32_t job,h1net_info *info);
int h1net_http_read(h1net_client *client,uint32_t job,uint32_t offset,
                   uint8_t *data,unsigned wanted,unsigned *received);
int h1net_http_cancel(h1net_client *client,uint32_t job);
int h1net_http_meta(h1net_client *client,uint32_t job,char url[H1NET_MAX_URL+1],char type[97]);
int h1net_download(h1net_client *client,const char *url,uint32_t max_bytes,
                  uint32_t timeout_ms,h1net_sink_fn sink,h1net_progress_fn progress,
                  void *context,h1net_info *result);
int h1net_download_range(h1net_client *client,const char *url,const h1net_range *range,
                        h1net_sink_fn sink,h1net_progress_fn progress,void *context,h1net_info *result);
#endif
