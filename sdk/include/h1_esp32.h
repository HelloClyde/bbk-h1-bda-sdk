/* SPDX-License-Identifier: Apache-2.0 */
#ifndef H1_ESP32_H
#define H1_ESP32_H
#ifdef __cplusplus
extern "C" {
#endif
#include "../esp32/common/h1net_client.h"
#include "../esp32/common/h1net_wifi_config.h"

/* Optional library: link with h1-bda-build --esp32. V1.41 native backend only.
 * All objects must be zero-initialized before first use. One foreground owner.
 * poll is cooperative. Other operations wait for individual bounded RPCs;
 * io.idle must pump UI/audio, but must NOT reenter this session. No hidden heap. */
enum h1_esp32_state {
    H1_ESP32_WAIT_USB, H1_ESP32_CHECKING, H1_ESP32_WIFI_OFFLINE,
    H1_ESP32_READY, H1_ESP32_DEVICE_ERROR
};
enum h1_esp32_error {
    H1_ESP32_AGAIN=-7, H1_ESP32_UNSUPPORTED=-8, H1_ESP32_STALE=-9,
    H1_ESP32_ARGUMENT=-10, H1_ESP32_LIMIT=-11, H1_ESP32_HTTP=-12,
    H1_ESP32_OWNER_BUSY=-13, H1_ESP32_FIRMWARE=-14
}; /* -1..-6: h1net_client_error; distinct remote/backend/status fields below. */
typedef struct {
    int code;
    uint32_t remote_status, http_status, backend_error;
} h1_esp32_error_info;
typedef struct {
    h1net_io io;
    void (*poll)(void *);
    int (*connected)(void *);
    uint32_t (*epoch)(void *);
    void (*bind)(void *);
    void (*close)(void *); /* optional; called by h1_esp32_close */
} h1_esp32_driver;
typedef struct {
    h1net_client client;
    h1net_rpc_task probe;
    h1_esp32_driver driver;
    h1_esp32_error_info error;
    uint32_t generation, epoch, next_probe, status[6];
    unsigned phase, busy, attached, native;
    enum h1_esp32_state state;
} h1_esp32;
typedef struct {
    h1_esp32 *owner;
    uint32_t job, generation, offset, maximum;
    h1net_info info;
    int eof;
} h1_esp32_stream;
typedef struct {
    const char *url;
    const char *method;       /* NULL = GET; GET and POST supported. */
    const char *headers;      /* NULL or CRLF ASCII fields; never auto-redirected. */
    const void *body;
    unsigned body_length;
    uint32_t max_bytes;       /* 0 = 256 KiB; hard cap 8 MiB per job. */
    uint32_t timeout_ms;      /* 0 = 30 s; otherwise 1000..60000. */
} h1_esp32_request;
enum h1_esp32_wifi_auth {
    H1_ESP32_AUTH_OPEN=0, H1_ESP32_AUTH_WPA_PSK=2, H1_ESP32_AUTH_WPA2_PSK=3,
    H1_ESP32_AUTH_WPA_WPA2_PSK=4, H1_ESP32_AUTH_WPA3_PSK=6,
    H1_ESP32_AUTH_WPA2_WPA3_PSK=7
};

/* Native: validates V1.41, cooperatively releases recognized H1Debug, and
 * software-reenumerates an attached cable. Never forcibly kills another task.
 * idle may be NULL. No timer-start/stop side effects. USB host must assert DTR. */
int h1_esp32_open(h1_esp32 *, void (*idle)(void *), void *context);
void h1_esp32_close(h1_esp32 *);
/* Injectable transport for tests / other hardware; does not own driver lifetime. */
int h1_esp32_init(h1_esp32 *, const h1_esp32_driver *);
enum h1_esp32_state h1_esp32_poll(h1_esp32 *);
uint32_t h1_esp32_features(const h1_esp32 *);
const char *h1_esp32_error_string(int);

int h1_esp32_http_open(h1_esp32 *, h1_esp32_stream *, const h1_esp32_request *);
int h1_esp32_http_range(h1_esp32 *, h1_esp32_stream *, const char *, const h1net_range *);
/* >0 bytes; 0 validated EOF; AGAIN = no buffered bytes yet; other <0 = error.
 * May return fewer than capacity bytes. Poll UI/audio between calls. */
int h1_esp32_http_read(h1_esp32 *, h1_esp32_stream *, void *, unsigned capacity);
/* Duplicate fields (especially Set-Cookie) preserved; NUL-terminated on success.
 * Fetch after response status is available, before closing stream. No logging. */
int h1_esp32_http_headers(h1_esp32 *, h1_esp32_stream *, char *, unsigned capacity);
/* Releases even partially read jobs. Stale handles never touch a new session. */
int h1_esp32_http_close(h1_esp32 *, h1_esp32_stream *);
/* Convenience GET: binary bytes, no terminator, 2xx required; closes on all paths.
 * No automatic replay. On error *length is the accepted partial byte count. */
int h1_esp32_get(h1_esp32 *, const char *, void *, unsigned capacity, unsigned *length);

int h1_esp32_wifi_status(h1_esp32 *, h1wifi_status *);
int h1_esp32_wifi_scan(h1_esp32 *, uint32_t *scan_id);
/* count is input capacity (<=12), output received. AGAIN while scan runs. */
int h1_esp32_wifi_page(h1_esp32 *, uint32_t scan_id, unsigned offset,
                       h1wifi_ap *, unsigned *count, unsigned *total);
int h1_esp32_wifi_connect(h1_esp32 *, const char *ssid, const char *password,
                          unsigned auth, int reuse_saved, uint32_t *operation);
int h1_esp32_wifi_cancel(h1_esp32 *, uint32_t operation);
int h1_esp32_wifi_forget(h1_esp32 *, uint32_t *operation);
/* Optional generic AAC extension. Application owns demux and playback clock. */
int h1_esp32_audio_rpc(h1_esp32 *, unsigned opcode, const void *, unsigned length,
                       void *, unsigned capacity, unsigned *received);
#ifdef __cplusplus
}
#endif
#endif
