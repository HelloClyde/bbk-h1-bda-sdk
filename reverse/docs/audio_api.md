# H1 native PCM and system volume services

Status: statically confirmed from the H1 V1.41 factory applications. The
descriptor lifecycle and initialization layout have two independent original
callers. Owner testing has confirmed normal physical-H1 playback for the SDK
DOOM port and the A320 Doudizhu and Zhao Yun ports. The remaining A320 titles
still require retesting after the common alignment and queue correction.
The V1.41 PCM wrappers and system volume wrappers are now exposed through
the opt-in `h1_v141.h` facade; this file retains their reverse-engineering evidence.

## Service table

The service table is loaded from `0x83C0000C`. The following offsets form one
audio controls and PCM lifecycle:

| Offset | Observed operation | Confirmed arguments |
| ---: | --- | --- |
| `+0x40` | set system-wide volume | `a0 = signed integer, clamped to 0..100`; void return |
| `+0x44` | read system-wide volume | no arguments; `v0 = integer 0..100` |
| `+0x50` | initialize one PCM descriptor | `a0 = descriptor` |
| `+0x54` | destroy one PCM descriptor | `a0 = descriptor` |
| `+0x58` | initialize the output device | `a0 = configuration` |
| `+0x5C` | submit/play a descriptor | `a0 = route`, `a1 = descriptor`, `a2/a3 = flags` |
| `+0x60` | start output | no observed arguments |
| `+0x64` | stop output | no observed arguments |
| `+0x68` | close output | no observed arguments |

The names are descriptive and are not recovered vendor symbols. PCM return-value
semantics and the exact meanings of the submit route/flags are not yet public
contracts.

## System volume (V1.41)

The public wrappers are `h1_audio_set_volume(int)` and
`h1_audio_get_volume(int *)` in `sdk/include/h1_audio.h`. They return a boolean;
the setter's result indicates a recognized void service was invoked, and the
getter preserves its output on failure. Both reject an unknown service pair.
Volume belongs to the system, not a descriptor. Save it before opening PCM,
change it after successful open, and restore it after shutdown on every path.

Static analysis of the V1.41 kernel (load base `0x80004000`, SHA-256
`d05786e442f9aad62a8d0a0cb4f6d786bdc7c2fa353a7a2b152c9ed9f01b40ef`)
identified SYS table `0x802A9EF0`. `+0x40` resolves to `0x801E05C4`:
it clamps a signed value to 0..100, stores it at `0x8095F350`, and calls
`0x801EA900(3, value)`. `+0x44` resolves to `0x801E0604` and returns
that global via an `lw` in its return delay slot. These addresses are evidence
only; SDK callers resolve entries from the runtime service table.

The recognized leading instruction signatures are:

```text
set[0] == 0x28850065        slti a1,a0,101
set[1] == 0x24030064        addiu v1,zero,100
 (get[0] & 0xffff0000) == 0x3c020000  lui v0,...
get[1] == 0x03e00008        jr ra
 (get[2] & 0xffff0000) == 0x8c420000  lw v0,...(v0)
```

This is a limited ABI guard for known V1.41 firmware, not general version
detection. No dB interpretation or perceptual linearity is established.

Runtime evidence is frozen in GameBoy v0.12.4's
[normal build report](https://github.com/HelloClyde/BBKH1-GBA/blob/v0.12.4/docs/evidence/v0.12.4-native-normal.json)
and [profile build report](https://github.com/HelloClyde/BBKH1-GBA/blob/v0.12.4/docs/evidence/v0.12.4-native-profile.json).
Both ran against the full V1.41 firmware in QEMU, retained running GBA/GB/GBC
cores across volume changes, and restored the original system volume of 51.
Captured audio at 70 was nonzero; at 0 all samples were zero. This verifies
the firmware services through GameBoy, not a fresh physical-device test or
execution of the new SDK example. `tests/fixtures/audio_volume.c` separately
executes the new SDK wrappers against MIPS service stubs.

## PCM descriptor

Factory applications allocate descriptors at a 32-byte stride. Before calling
`SYS+0x50`, they write:

```c
struct h1_pcm_descriptor_candidate {
    void *pcm;          /* +0x00 */
    uint32_t bytes;     /* +0x04, even byte count in the chess sample */
    uint32_t private_words[6];
};
```

The remaining 24 bytes are zero before initialization and belong to the H1
audio service. Applications preserve them between `SYS+0x50` and `SYS+0x54`.
They must therefore not be replaced by the eight-byte `{pcm, bytes}` structure
previously used by the A320 compatibility layer.

In the factory Chinese Chess BDA, a table of 20 descriptors begins at runtime
address `0x83C12EF8`. Each source record contributes a PCM pointer and
`(record_bytes - 12) & ~1`; `SYS+0x50` is called once per descriptor. Playback
uses `SYS+0x5C(0, descriptor, 0, 0)`. Shutdown calls `SYS+0x54` once per
initialized descriptor before releasing the source data.

The factory Mission BDA independently uses 32-byte descriptor arrays and calls
`SYS+0x50` only after both leading words are nonzero. Its mixer uses other
submit route/flag combinations, proving that those arguments select behavior
rather than descriptor size.

## Output configuration

Both factory samples pass a word array to `SYS+0x58`. Confirmed leading words
are:

```c
struct h1_pcm_config_candidate {
    uint32_t sample_rate;
    uint32_t mode;
    uint32_t buffer_bytes;
};
```

Mission clears a 36-byte backing object and writes `{11025, 1, 4096}` before
initialization. Chinese Chess writes `{caller_rate, caller_mode, 4096}`. This
confirms the sample-rate and buffer-size fields; `mode == 1` is consistent with
the factory mono path but its exact enum meaning remains provisional.

The observed startup order is descriptor initialization as needed,
`SYS+0x58(config)`, then `SYS+0x60()`. Shutdown is `SYS+0x64()`,
`SYS+0x68()`, followed by `SYS+0x54(descriptor)` for each live descriptor.

## Porting consequence

The old A320 bridge happened to produce audio in QEMU because the emulated
firmware consumed the two public words directly. Physical H1 hardware stayed
silent because the system-owned descriptor state was never initialized. A
hardware-capable bridge must retain a full descriptor, call `SYS+0x50` before
its first submission, and call `SYS+0x54` during teardown.
