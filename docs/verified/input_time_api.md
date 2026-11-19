# Verified input and time APIs

H1 native BDA applications can use:

```c
#include "h1_input.h"
#include "h1_time.h"
```

## Keyboard events

`h1_event_fetch(&code, &value)` reads the H1 full-keyboard queue. A physical key
transition uses:

- `H1_EVENT_KEY_DOWN` (`9`)
- `H1_EVENT_KEY_UP` (`10`)
- `value`: one of the `H1_KEY_*` matrix identifiers in `h1_input.h`

`h1_input_poll_key()` filters non-key events and returns at most one transition.
It discards non-key events and is suitable for simple key-only probes. Native
games needing touch/system events should drain `h1_event_fetch()` themselves.
For sustained/combined keys on V1.41, use `h1_game_input_sample()` after feeding
events to `h1_game_input_event()`; event identifiers differ from native game
scancodes. See [the native game guide](../native-games.md).

## Power button on H1/Y100 V1.41

`h1_event_fetch()` and `h1_input_poll_key()` handle a pending system shutdown
before reading the keyboard queue. Standard applications pick up this behavior
when rebuilt with the updated headers. A custom foreground loop that bypasses
both input APIs can call `h1_power_poll()` at its regular polling boundary.
`h1_event_fetch_raw()` is available for tools that need only the raw queue.

The firmware finishes shutdown through its native GUI event service by
default. Applications with pending saves or resources to close can call
`h1_power_run(run)` on the foreground thread. A power request observed by the
SDK during `run` exits that callback and returns `1`. The caller then performs
its normal cleanup and returns from the BDA entry. Keep cleanup outside the
callback and finish it before calling any managed polling API again. In C++,
keep objects that require destructors outside this callback. The SDK build
command links the runtime for `h1_power_run()` automatically. A custom link
command can include `sdk/src/h1_power.S` when using that API.

## Clocks

- `h1_raw_tick_80hz()` is a continuously running 80 Hz monotonic tick.
- `h1_timer_start()` clears and starts a private 1 ms counter.
- `h1_timer_read_ms()` reads the current counter.
- `h1_timer_stop()` stops it and must be paired with `h1_timer_start()`.

The 1 ms timer generates `H1_EVENT_TIMER` housekeeping events in the same queue;
`h1_input_poll_key()` filters them.

These semantics were measured by `H1InputTimeProbe.bda` in the ARM64-hosted H1
emulator over a 12,000 ms capture.

With PCM enabled, physical H1 observations found the software millisecond
counter can lag audio progress. Do not use it alone to pace an audio stream or
as an accurate cost profiler. Raw ticks have 12.5 ms resolution. JZ4740 has no
CP0 Count; a QEMU implementation accepting it is not hardware evidence. The
explicitly experimental TCU5 clock has ownership, wrap and restore rules in
`sdk/include/experimental/h1_profile_clock.h`.
