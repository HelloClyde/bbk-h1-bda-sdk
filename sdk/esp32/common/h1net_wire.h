/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#ifndef H1NET_WIRE_H
#define H1NET_WIRE_H
#include <stddef.h>
#include <stdint.h>

#define H1NET_VERSION 1u
#define H1NET_HEADER 12u
#define H1NET_MAX_PAYLOAD 512u
#define H1NET_MAX_FRAME (H1NET_HEADER + H1NET_MAX_PAYLOAD + 4u)
#define H1NET_IDENTITY "H1NET/1 ESP32 HTTP"
#define H1NET_MAX_URL 400u
#define H1NET_RANGE_URL 2048u
#define H1NET_MAX_DOWNLOAD 8388608u
#define H1NET_MAX_JOBS 4u
#define H1NET_CAP_RELEASE 1u
#define H1NET_CAP_BACKGROUND 2u
#define H1NET_CAP_AUDIO_AAC 4u
#define H1NET_CAP_HTTP_REQUEST 8u
#define H1NET_CAP_WIFI_CONFIG 16u
#define H1NET_CAP_HTTP_HEADERS 32u
#define H1NET_MAX_HEADERS 1024u
#define H1NET_MAX_REQUEST_BODY 16384u
#define H1NET_MAX_REQUEST (H1NET_RANGE_URL+H1NET_MAX_HEADERS+H1NET_MAX_REQUEST_BODY)

enum h1net_opcode {
    H1NET_HELLO=1, H1NET_STATUS=2, H1NET_HTTP_OPEN=3,
    H1NET_HTTP_INFO=4, H1NET_HTTP_READ=5, H1NET_HTTP_CANCEL=6, H1NET_HTTP_META=7,
    H1NET_URL_CHUNK=8, H1NET_HTTP_RANGE=9,
    H1NET_CAPABILITIES=10, H1NET_HTTP_RELEASE=11, H1NET_HTTP_OPEN_BACKGROUND=12,
    H1NET_AUDIO_OPEN=13,H1NET_AUDIO_PUSH=14,H1NET_AUDIO_READ=15,H1NET_AUDIO_INFO=16,
    H1NET_AUDIO_RESET=17,H1NET_AUDIO_CLOSE=18,H1NET_AUDIO_FINISH=19,
    H1NET_REQUEST_CHUNK=20,H1NET_HTTP_REQUEST=21,
    H1NET_WIFI_SCAN=22,H1NET_WIFI_PAGE=23,H1NET_WIFI_STATUS=24,
    H1NET_WIFI_CONNECT=25,H1NET_WIFI_FORGET=26,H1NET_WIFI_CANCEL=27,
    H1NET_HTTP_HEADERS=28
};
enum h1net_status {
    H1NET_OK=0, H1NET_INVALID=1, H1NET_BUSY=2,
    H1NET_NOT_CONNECTED=3, H1NET_BAD_JOB=4, H1NET_INTERNAL=5
};
enum h1net_job_state {
    H1NET_IDLE=0, H1NET_CONNECTING=1, H1NET_RECEIVING=2,
    H1NET_DONE=3, H1NET_FAILED=4, H1NET_CANCELLED=5
};
enum h1net_job_error {
    H1NET_ERROR_NONE=0, H1NET_ERROR_NETWORK=1, H1NET_ERROR_LIMIT=2,
    H1NET_ERROR_TIMEOUT=3, H1NET_ERROR_CANCELLED=4,
    H1NET_ERROR_WIFI=5, H1NET_ERROR_HTTP_PROTOCOL=6
};
typedef struct {
    uint8_t opcode;
    uint16_t sequence, status, length;
    const uint8_t *payload;
} h1net_frame;
typedef struct {
    uint8_t data[H1NET_MAX_FRAME];
    unsigned used, expected;
    uint32_t bad_headers, bad_crc;
} h1net_parser;
typedef void (*h1net_frame_fn)(void *context, const h1net_frame *frame);

uint16_t h1net_u16(const uint8_t *p);
uint32_t h1net_u32(const uint8_t *p);
void h1net_put16(uint8_t *p, uint16_t value);
void h1net_put32(uint8_t *p, uint32_t value);
uint32_t h1net_crc32(const void *data, size_t length);
size_t h1net_encode(uint8_t *out, size_t capacity, uint8_t opcode,
                    uint16_t sequence, uint16_t status,
                    const void *payload, size_t length);
void h1net_parser_init(h1net_parser *parser);
void h1net_parser_feed(h1net_parser *parser, const uint8_t *data, size_t length,
                      h1net_frame_fn callback, void *context);
int h1net_valid_url(const uint8_t *url, size_t length);
int h1net_valid_long_url(const uint8_t *url, size_t length);
/* CRLF-separated ASCII Name: value fields; an optional final CRLF is allowed. */
int h1net_valid_headers(const uint8_t *headers, size_t length);
void h1net_secure_clear(void *data,size_t length);
#endif
