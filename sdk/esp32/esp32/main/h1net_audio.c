/* SPDX-License-Identifier: Apache-2.0
 * H1 ESP32 contributors. SDK import with copyright-holder authorization. */
#include "h1net_audio.h"
#include "h1net_wire.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "esp_aac_dec.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* Fixed limits include incomplete fragments, queued frames and decoder scratch.
 * Espressif's codec has its own additional, profile-dependent allocation. */
#define AUDIO_RATE 32768u
#define FRAME_MAX 16384u
#define INPUT_SLOTS 2u
#define PCM_CAPACITY (128u * 1024u)
#define DECODE_CAPACITY 16384u
#define ARENA_SIZE (INPUT_SLOTS * FRAME_MAX + FRAME_MAX + DECODE_CAPACITY + PCM_CAPACITY)
#define PCM_BLOCK 512u
#define STATE_READY 1u
#define STATE_EOF 2u
#define STATE_FAILED 3u

typedef struct {
    uint32_t total, used, pts;
} frame_slot;
typedef struct {
    bool active, finished;
    uint32_t id, epoch, state, error, next_frame;
    uint32_t input_bytes, output_bytes, read_bytes;
    uint32_t core_rate;
    uint8_t core_channels, asc[64];
    unsigned asc_length;
    frame_slot frames[INPUT_SLOTS];
    unsigned input_head, input_tail, input_count;
    unsigned pcm_head, pcm_tail, pcm_count;
} audio_job;
typedef struct {
    const uint8_t *data;
    unsigned bits, at;
    bool failed;
} bit_reader;
typedef struct {
    uint32_t rate, phase;
    int64_t accumulator;
    uint8_t bytes[PCM_BLOCK];
    unsigned used;
} resampler;

static audio_job job;
static uint32_t next_job;
static SemaphoreHandle_t lock;
static TaskHandle_t task;
static uint8_t *arena, *input, *encoded, *decoded, *pcm;

static void take(void) { xSemaphoreTake(lock, portMAX_DELAY); }
static void give(void) { xSemaphoreGive(lock); }
static void wake(void) { if (task) xTaskNotifyGive(task); }
static void wait_for_work(void) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30) + 1); }

static uint32_t read_bits(bit_reader *r, unsigned n)
{
    uint32_t value = 0;
    if (n > 24 || r->at > r->bits || n > r->bits - r->at) {
        r->failed = true;
        return 0;
    }
    while (n--) {
        value = (value << 1) | ((r->data[r->at / 8] >> (7 - r->at % 8)) & 1);
        ++r->at;
    }
    return value;
}

static uint32_t read_rate(bit_reader *r)
{
    static const uint32_t rates[] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000,
        22050, 16000, 12000, 11025, 8000, 7350
    };
    unsigned index = read_bits(r, 4);
    uint32_t rate = index == 15 ? read_bits(r, 24) :
                    index < sizeof(rates) / sizeof(rates[0]) ? rates[index] : 0;
    /* The official decoder advertises rates from 8 kHz through 96 kHz. */
    bool supported = false;
    for (unsigned i = 0; i < 12; ++i) if (rates[i] == rate) supported = true;
    if (!supported) r->failed = true;
    return rate;
}

static bool parse_asc(const uint8_t *asc, unsigned length,
                      uint32_t *core_rate, uint8_t *channels)
{
    bit_reader r = {asc, length * 8, 0, false};
    uint32_t object = read_bits(&r, 5);
    uint32_t rate = read_rate(&r), channel = read_bits(&r, 4);
    /* The direct API takes the AAC core rate/channels, rather than ASC bytes.
     * Explicit HE-AAC/HE-AACv2 ASC starts with the core rate, followed by the
     * SBR extension rate. Implicit SBR/PS is discovered by the enabled codec. */
    if (object == 5 || object == 29) {
        (void)read_rate(&r);
        object = read_bits(&r, 5);
    }
    if (r.failed || object != 2 || channel < 1 || channel > 2) return false;
    /* PCE, 960-sample frames, core coder dependency and GASpecific extensions
     * cannot be expressed by esp_aac_dec_cfg_t; reject rather than guess. */
    if (read_bits(&r, 1) || read_bits(&r, 1) || read_bits(&r, 1) || r.failed) return false;
    *core_rate = rate;
    *channels = (uint8_t)channel;
    return true;
}

static bool is_adts_frame(const uint8_t *data, unsigned length)
{
    if (length < 7 || data[0] != 0xff || (data[1] & 0xf6) != 0xf0) return false;
    unsigned header = (data[1] & 1) ? 7 : 9;
    unsigned rate_index = (data[2] >> 2) & 15;
    unsigned channels = ((data[2] & 1) << 2) | (data[3] >> 6);
    unsigned size = ((data[3] & 3) << 11) | (data[4] << 3) | (data[5] >> 5);
    return length >= header && size == length && rate_index < 12 &&
           channels >= 1 && channels <= 2 && (data[2] >> 6) == 1 && !(data[6] & 3);
}

/* Called with lock held. Keep ASC/config/id; epoch invalidates all worker data. */
static void reset_locked(void)
{
    ++job.epoch;
    job.state = STATE_READY;
    job.error = job.next_frame = job.input_bytes = job.output_bytes = job.read_bytes = 0;
    job.finished = false;
    job.input_head = job.input_tail = job.input_count = 0;
    job.pcm_head = job.pcm_tail = job.pcm_count = 0;
    memset(job.frames, 0, sizeof(job.frames));
}

static void fail_job(uint32_t epoch, uint32_t error)
{
    take();
    if (job.active && job.epoch == epoch) {
        job.state = STATE_FAILED;
        job.error = error;
    }
    give();
}

static bool publish(uint32_t epoch, const uint8_t *data, unsigned length)
{
    for (;;) {
        take();
        if (!job.active || job.epoch != epoch || job.state != STATE_READY) {
            give();
            return false;
        }
        if (length <= PCM_CAPACITY - job.pcm_count) {
            if (length > UINT32_MAX - job.output_bytes) {
                job.state = STATE_FAILED;
                job.error = H1NET_INTERNAL;
                give();
                return false;
            }
            unsigned first = PCM_CAPACITY - job.pcm_tail;
            if (first > length) first = length;
            memcpy(pcm + job.pcm_tail, data, first);
            memcpy(pcm, data + first, length - first);
            job.pcm_tail = (job.pcm_tail + length) % PCM_CAPACITY;
            job.pcm_count += length;
            job.output_bytes += length;
            give();
            return true;
        }
        give();
        wait_for_work();
    }
}

static bool resample_pcm(uint32_t epoch, resampler *r,
                         const esp_audio_dec_info_t *info, unsigned size)
{
    if (info->bits_per_sample != 16 || info->channel < 1 || info->channel > 2 ||
        info->sample_rate < 8000 || info->sample_rate > 96000 ||
        size > DECODE_CAPACITY || size % (2u * info->channel)) return false;
    if (r->rate != info->sample_rate) {
        r->rate = info->sample_rate;
        r->phase = 0;
        r->accumulator = 0;
    }
    for (unsigned i = 0; i < size; i += 2u * info->channel) {
        int32_t sample = (int16_t)((uint16_t)decoded[i] | ((uint16_t)decoded[i + 1] << 8));
        if (info->channel == 2) {
            int32_t right = (int16_t)((uint16_t)decoded[i + 2] | ((uint16_t)decoded[i + 3] << 8));
            sample = (sample + right) / 2;
        }
        /* Identical weighted-box algorithm to src/platform/audio.c. int64
         * also safely covers 88.2/96 kHz full-scale input. */
        unsigned remaining = AUDIO_RATE;
        while (remaining) {
            unsigned weight = r->rate - r->phase;
            if (weight > remaining) weight = remaining;
            r->accumulator += (int64_t)sample * weight;
            r->phase += weight;
            remaining -= weight;
            if (r->phase == r->rate) {
                int16_t out = (int16_t)(r->accumulator / r->rate);
                r->bytes[r->used++] = (uint8_t)out;
                r->bytes[r->used++] = (uint8_t)((uint16_t)out >> 8);
                r->phase = 0;
                r->accumulator = 0;
                if (r->used == sizeof(r->bytes)) {
                    if (!publish(epoch, r->bytes, r->used)) return false;
                    r->used = 0;
                }
            }
        }
    }
    if (r->used && !publish(epoch, r->bytes, r->used)) return false;
    r->used = 0;
    return true;
}

static void worker(void *unused)
{
    (void)unused;
    uint32_t epoch = UINT32_MAX;
    void *decoder = NULL;
    resampler filter = {0};
    for (;;) {
        take();
        if (epoch != job.epoch) {
            epoch = job.epoch;
            give();
            if (decoder) esp_aac_dec_close(decoder);
            decoder = NULL;
            memset(&filter, 0, sizeof(filter));
            continue;
        }
        if (!job.active || job.state != STATE_READY || !job.input_count) {
            if (job.active && job.state == STATE_READY && job.finished) job.state = STATE_EOF;
            give();
            wait_for_work();
            continue;
        }
        unsigned length = job.frames[job.input_head].total;
        memcpy(encoded, input + job.input_head * FRAME_MAX, length);
        memset(&job.frames[job.input_head], 0, sizeof(frame_slot));
        job.input_head = (job.input_head + 1) % INPUT_SLOTS;
        --job.input_count;
        esp_aac_dec_cfg_t config = {
            .sample_rate = (int32_t)job.core_rate,
            .channel = job.core_channels,
            .bits_per_sample = 16,
            .no_adts_header = !is_adts_frame(encoded, length),
            .aac_plus_enable = true
        };
        give();
        if (!decoder && esp_aac_dec_open(&config, sizeof(config), &decoder) != ESP_AUDIO_ERR_OK) {
            fail_job(epoch, H1NET_INTERNAL);
            continue;
        }
        esp_audio_dec_in_raw_t raw = {.buffer = encoded, .len = length};
        while (raw.len) {
            esp_audio_dec_out_frame_t out = {.buffer = decoded, .len = DECODE_CAPACITY};
            esp_audio_dec_info_t info = {0};
            raw.consumed = 0;
            esp_audio_err_t error = esp_aac_dec_decode(decoder, &raw, &out, &info);
            if (error != ESP_AUDIO_ERR_OK || !raw.consumed || raw.consumed > raw.len ||
                !resample_pcm(epoch, &filter, &info, out.decoded_size)) {
                fail_job(epoch, H1NET_INTERNAL);
                break;
            }
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
        }
        /* Lower priority than USB/HTTP; also release a time slice even when a
         * reader continuously drains output and compressed frames are queued. */
        vTaskDelay(1);
    }
}

bool h1net_audio_init(void)
{
    if (task) return true;
    if (!lock) lock = xSemaphoreCreateMutex();
    if (!lock) return false;
    /* Refuse audio without PSRAM instead of taking 192 KiB from internal RAM
     * shared by TLS, Wi-Fi and USB. Other H1NET capabilities remain available. */
    arena = heap_caps_malloc(ARENA_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!arena) return false;
    input = arena;
    encoded = input + INPUT_SLOTS * FRAME_MAX;
    decoded = encoded + FRAME_MAX;
    pcm = decoded + DECODE_CAPACITY;
    if (xTaskCreate(worker, "h1_aac", 24 * 1024, NULL, 2, &task) != pdPASS) {
        free(arena);
        arena = input = encoded = decoded = pcm = NULL;
        task = NULL;
        return false;
    }
    return true;
}

void h1net_audio_close(void)
{
    if (!lock) return;
    take();
    job.active = false;
    reset_locked();
    give();
    wake();
}

bool h1net_audio_dispatch(uint8_t op, const uint8_t *p, unsigned length,
                         uint16_t *status, uint8_t *response, unsigned *response_length)
{
    if (op < H1NET_AUDIO_OPEN || op > H1NET_AUDIO_FINISH) return false;
    *response_length = 0;
    *status = H1NET_INVALID;
    if ((!p && length) || length > H1NET_MAX_PAYLOAD) return true;
    if (!task) { *status = H1NET_NOT_CONNECTED; return true; }
    take();
    if (op == H1NET_AUDIO_OPEN) {
        if (length < 17 || h1net_u32(p) != 1 || h1net_u32(p + 4) != AUDIO_RATE ||
            h1net_u32(p + 8) != 1 || h1net_u32(p + 12) != length - 16 ||
            length - 16 > sizeof(job.asc)) goto done;
        uint32_t rate;
        uint8_t channels;
        if (!parse_asc(p + 16, length - 16, &rate, &channels)) goto done;
        if (job.active) { *status = H1NET_BUSY; goto done; }
        reset_locked();
        if (!++next_job) ++next_job;
        job.id = next_job;
        job.active = true;
        job.core_rate = rate;
        job.core_channels = channels;
        job.asc_length = length - 16;
        memcpy(job.asc, p + 16, job.asc_length);
        h1net_put32(response, job.id);
        *response_length = 4;
        *status = H1NET_OK;
        goto done;
    }
    if (length < 4) goto done;
    if (!job.active || !h1net_u32(p) || h1net_u32(p) != job.id) {
        *status = H1NET_BAD_JOB;
        goto done;
    }
    switch (op) {
    case H1NET_AUDIO_PUSH: {
        if (length <= 20 || job.finished || job.state != STATE_READY) break;
        uint32_t sequence = h1net_u32(p + 4), total = h1net_u32(p + 8);
        uint32_t offset = h1net_u32(p + 12), pts = h1net_u32(p + 16);
        unsigned count = length - 20;
        if (!total || total > FRAME_MAX || offset > total || count > total - offset ||
            sequence != job.next_frame || count > UINT32_MAX - job.input_bytes) break;
        if (job.input_count == INPUT_SLOTS) { *status = H1NET_BUSY; break; }
        frame_slot *slot = &job.frames[job.input_tail];
        if (offset != slot->used || (slot->used && (slot->total != total || slot->pts != pts))) break;
        slot->total = total;
        slot->pts = pts;
        memcpy(input + job.input_tail * FRAME_MAX + offset, p + 20, count);
        slot->used += count;
        job.input_bytes += count;
        h1net_put32(response, slot->used);
        *response_length = 4;
        *status = H1NET_OK;
        if (slot->used == slot->total) {
            ++job.input_count;
            job.input_tail = (job.input_tail + 1) % INPUT_SLOTS;
            ++job.next_frame;
        }
        break;
    }
    case H1NET_AUDIO_READ: {
        if (length != 8) break;
        uint32_t wanted = h1net_u32(p + 4);
        if (!wanted || wanted > 500) break;
        if (!job.pcm_count && job.state == STATE_FAILED) { *status = H1NET_INTERNAL; break; }
        unsigned count = wanted < job.pcm_count ? wanted : job.pcm_count;
        h1net_put32(response, job.id);
        h1net_put32(response + 4, job.read_bytes);
        h1net_put32(response + 8, count);
        unsigned first = PCM_CAPACITY - job.pcm_head;
        if (first > count) first = count;
        memcpy(response + 12, pcm + job.pcm_head, first);
        memcpy(response + 12 + first, pcm, count - first);
        job.pcm_head = (job.pcm_head + count) % PCM_CAPACITY;
        job.pcm_count -= count;
        job.read_bytes += count;
        *response_length = 12 + count;
        *status = H1NET_OK;
        break;
    }
    case H1NET_AUDIO_INFO:
        if (length != 4) break;
        h1net_put32(response, job.id);
        h1net_put32(response + 4, job.state);
        h1net_put32(response + 8, AUDIO_RATE);
        h1net_put32(response + 12, 1);
        h1net_put32(response + 16, job.input_bytes);
        h1net_put32(response + 20, job.output_bytes);
        h1net_put32(response + 24, job.pcm_count);
        h1net_put32(response + 28, job.error);
        *response_length = 32;
        *status = H1NET_OK;
        break;
    case H1NET_AUDIO_RESET:
        if (length != 4) break;
        reset_locked();
        *status = H1NET_OK;
        break;
    case H1NET_AUDIO_CLOSE:
        if (length != 4) break;
        job.active = false;
        reset_locked();
        *status = H1NET_OK;
        break;
    case H1NET_AUDIO_FINISH:
        if (length != 4 || job.state != STATE_READY ||
            (job.input_count < INPUT_SLOTS && job.frames[job.input_tail].used)) break;
        job.finished = true;
        *status = H1NET_OK;
        break;
    default:
        break;
    }
done:
    give();
    wake();
    return true;
}
