/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#ifndef H1NET_AUDIO_H
#define H1NET_AUDIO_H
#include <stdbool.h>
#include <stdint.h>

/* Call once from app initialization before dispatching requests. Failure keeps
 * the service offline without preventing HTTP, USB or Wi-Fi from starting. */
bool h1net_audio_init(void);
/* Cancel the current job. The worker owns and asynchronously closes its codec. */
void h1net_audio_close(void);
/* response points to at least 512 bytes; response_length is output-only.
 * Returns false for opcodes outside the generic AUDIO_OPEN..AUDIO_FINISH range.
 * The transport serializes dispatches, just as for the existing HTTP backend. */
bool h1net_audio_dispatch(uint8_t op, const uint8_t *payload, unsigned length,
                         uint16_t *status, uint8_t *response,
                         unsigned *response_length);
#endif
