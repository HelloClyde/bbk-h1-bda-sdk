# Generic AAC worker dependency and validation

`main/idf_component.yml` pins `espressif/esp_audio_codec: "==2.6.2"`.
`main/CMakeLists.txt` includes `h1net_audio.c` and requires `esp_audio_codec`.
This backend directly calls the AAC decoder;
it does not register the component's other encoders or decoders.

The HTTP backend calls `h1net_audio_init()` once at boot and supplies the
generic compute callback only on success. The shared server then adds
`H1NET_CAP_AUDIO_AAC=4` to the HTTP feature bits: current firmware features are 63 when audio
is available, otherwise 59; always test individual capability bits. The four task slots and 8192-byte capability field
still describe HTTP. Audio has one separate job; its numeric ID may equal an
HTTP ID. Requests share the same serialized RPC sequence, and audio RESET or
CLOSE leaves HTTP jobs and cursors intact.

The complete wire layouts are in [PROTOCOL.md](../PROTOCOL.md). Operations
13–19 are OPEN, PUSH, READ, INFO, RESET, CLOSE and FINISH. OPEN accepts version
1, output rate 32768, one output channel and 1–64 ASC bytes. PUSH assembles one
1–16384-byte AAC access unit using contiguous 1–492-byte fragments. Its frame
sequence starts at zero after OPEN/RESET and advances only after a complete
frame is accepted. Fragment PTS and total must agree within a frame; PTS does
not schedule gaps or silence in the PCM output.

A BUSY PUSH consumes none of that fragment, including a final fragment.
Drain PCM or allow worker progress, then retry the same payload with the next
RPC sequence. ESP can return BUSY when both compressed-frame slots are full;
the synchronous PC backend can reject a final fragment before decoding when
PCM space is insufficient. A timeout does not provide this non-consumption
guarantee, so it does not authorize an automatic replay.

READ accepts a byte count of 1–500 and returns a contiguous byte offset and
signed little-endian 16-bit mono PCM at 32768 Hz. An empty successful READ is
normal while READY. INFO uses audio-specific states 1 READY, 2 EOF, 3 FAILED;
only EOF plus buffered=0 means all output has been drained. It reports input
AAC bytes, total produced PCM bytes, unread PCM bytes and an error code
(normally 0, currently INTERNAL=5 on decoder failure). A prior partial output
does not turn FAILED into a successful stream.

RESET preserves job ID and ASC, clears incomplete/queued input and unread
output, resets frame sequence and all byte cursors, clears EOF/error and
returns to READY. Worker epoch checks prevent pre-reset output from leaking
into the new stream. FINISH requires no incomplete frame and stops further
PUSH; its acknowledgment is followed by READ/INFO until EOF and an empty
output ring. The ESP worker completes queued frames asynchronously. The PC
backend can return BUSY before FINISH flushes; drain output and retry. Clients
should not depend on repeated FINISH after EOF: PC currently accepts it,
whereas the ESP implementation requires READY for FINISH.

The generic callbacks route these operations through
`h1net_audio_dispatch()`. A successful HELLO, a transport disconnect or
`h1net_audio_close()` invalidates the current audio job; the worker closes its
private codec asynchronously. AUDIO_CLOSE immediately invalidates the ID and
discards pending data. The arena and task remain allocated for later OPENs.

The worker allocates a 192 KiB PSRAM arena: two 16 KiB compressed-frame
slots, 16 KiB private compressed input, 16 KiB private decoded PCM, and a
128 KiB output ring. Its internal-RAM task stack is 24 KiB; the codec has
additional profile-dependent allocations. Missing PSRAM or failed task/arena
initialization leaves audio unavailable and omits capability 4. The large
arena never falls back to internal RAM shared by Wi-Fi, USB and TLS workers.
The priority-2 worker yields one tick after each compressed frame; decode and
resampling do not execute in the USB dispatch task. This scheduling choice is
not a measured throughput guarantee.

The direct vendor API accepts raw AAC rate/channel configuration, rather than
an ASC byte blob. The backend therefore validates the supported ASC subset:
AAC-LC core (object type 2), optional explicit HE-AAC/HE-AACv2 outer object
(5/29), mono/stereo, supported standard rates 8–96 kHz, 1024-sample core
frames, no core-coder dependency or GASpecific extension flag. It preserves
the original ASC through RESET and enables AAC-plus processing. PCE and
960-sample configurations are rejected. A complete, valid AAC-LC ADTS header
on the first frame selects the vendor's ADTS mode instead of raw mode.
Implicit SBR/PS is left to the enabled decoder. Actual stream compatibility
still requires codec execution on the ESP32-S3.

Decoded mono/stereo 16-bit PCM is downmixed using C integer division toward
zero, then converted to 32768 Hz using the weighted-box algorithm using integer arithmetic in this backend. Fractional position persists across frames; a widened
accumulator safely covers 88.2/96 kHz. The final incomplete output interval is
discarded, as in the H1 baseline. PTS is checked for fragment consistency;
the worker emits contiguous PCM and does not insert timing gaps.

Official dependency references checked 2026-11-12:

- [Component 2.6.2](https://components.espressif.com/components/espressif/esp_audio_codec/versions/2.6.2): AAC-LC, HE-AAC and HE-AACv2 support; ESP32-S3 target.
- [AAC API header](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_codec/include/decoder/impl/esp_aac_dec.h): direct open/decode/close APIs and raw configuration fields.
- [Decoder API header](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_codec/include/decoder/esp_audio_dec.h): consumed input and decoded-output fields.
- [Official direct-decoder test](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_codec/test_apps/audio_codec_test/main/audio_decoder_test.c): frame decoding loop and codec return handling.
- [2.6.2 changelog](https://components.espressif.com/components/espressif/esp_audio_codec/versions/2.6.2/changelog): includes an AAC-LC mono output packing fix.
- [2.6.2 license](https://components.espressif.com/components/espressif/esp_audio_codec/versions/2.6.2/license): Espressif Modified MIT, `LicenseRef-Espressif-Modified-MIT`. This dependency is restricted to use with Espressif products; preserve its copyright and license notices with redistributed firmware/components. It is not the PC simulator decoder.

Verification history and current physical/model boundaries are summarized in
[esp32-verification.md](../../../docs/esp32-verification.md). This import does
not re-run old media/business tests or publish proprietary input media.
