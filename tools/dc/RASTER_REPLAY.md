# Synthetic PVR correctness replay

The replay uses the production RDP, software rasterizer and (on KOS) PVR
backend. It contains synthetic data only and needs no game ROM. It checks
post-readback RGBA5551 words before VI conversion, not screenshots.

## Run the reference

From the repository root in the existing ARM32 Docker build environment:

```sh
make -f tests/dc/Makefile.raster-replay HOST=1
qemu-arm -L /usr/arm-linux-gnueabihf build/dc/raster-replay-1-0/replay.elf > reference.log
python3 tools/dc/check_raster_replay.py reference.log
```

## Run the actual GPU path

With the current KOS environment loaded, from the repository root:

```sh
make -f tests/dc/Makefile.raster-replay STRICT=1
```

Boot `build/dc/raster-replay-0-1/replay.elf` in the isolated Flycast 2.6 setup,
using OpenGL, serial console enabled, `rend.RenderToTextureBuffer=yes`, and
`rend.EmulateFramebuffer=no`. Capture that process's console using
`tools/dc/scrape_console.ps1`, then validate:

```sh
python3 tools/dc/check_raster_replay.py --gpu strict.log
```

The target prints one completion summary and stays idle so serial results can
be captured. Close that test instance after collection. The checker rejects
missing, repeated, truncated or failing fixture results. `--gpu` also requires
completed scenes for every fixture that is supposed to exercise hardware.

For the negative control, build with `STRICT=0` and boot
`build/dc/raster-replay-0-0/replay.elf`. This mode currently fails the suite;
the checker must return nonzero. A successful build is not a successful replay.

## What is tested

| Fixture | Independent expectation / comparison |
| --- | --- |
| Two disjoint opaque fills | All 76,800 framebuffer words: only covered pixels change; the gap retains RGB and alternating alpha |
| 512 opaque fills | Every input channel value, exact five-bit output; at least two GPU scenes exercise the submission limit |
| GPU/software/GPU transition | Rejected alpha-test samples preserve the destination; an intervening CPU edit survives the next batch |
| Two-cycle producer, one-cycle consumer | Matches the software combiner's defined prior-state behavior on cold and hot cache candidates |

The first three have analytic expected framebuffer values. The last establishes
software parity; it does not independently establish the architectural meaning
of cross-draw COMBINED state on real N64 hardware.

## First result, 2026-09-21

Reference and strict KOS/PVR runs pass all five result lines with zero errors.
GPU scene counts are 1 for disjoint fills, 2 for the ramp, and 2 for the mixed
transition. The experimental backend fails all five: partial-fill alpha damage,
RGB differences, and a red producer followed by a black COMBINED consumer.
Evidence: ignored `build/dc/validation/quality-replay-{host,strict,fast}.log`.
The full existing ARM32 regression suite also passes (`quality-full-host.log`).

Strict mode preserves the exact write union with a 9,600-byte bit mask. It
disables additional PVR framebuffer dithering and quantizes opaque fill colors
before GPU submission. Five-bit channel values are submitted at their quantization
bin origins, so both truncating and rounded readback retain the intended value.
The rounded RGB565 conversion is visible in the official
[Flycast 2.6 framebuffer writer](https://github.com/flyinghead/flycast/blob/v2.6/core/rend/TexCache.cpp).

## Current strict-mode boundary

For a game build, add `RASTER_STRICT=1` to `RASTER_PVR=1 GFX=soft VIDEO=pvr`.
Only opaque one-cycle fills are accelerated. Textures, blended fills and fill-cycle
word operations go through the existing software renderer. These restrictions
are deliberate until equivalent implementations have target evidence.

This is an opt-in correctness baseline, not a new 5 FPS claim or a complete N64
renderer. It does not replace the Downloads launcher. Wider replay coverage,
independent filtering/coverage/depth reference results, gameplay scenes and
hardware validation remain in [the quality plan](FPS_QUALITY_PLAN.md).

## Strict OOT timing baseline

The separate `not64-oot-quality-strict.cdi` validation image loads the menu
checkpoint and starts 32006 Hz AICA. `quality-strict-game.log` records frame 60
at 117,843,419 us and frame 120 at 234,161,240 us: **60 / 116.317821 = 0.516 FPS**.
This is one warmed-up interval, not a release performance qualification. No
PVR scene batch counters appeared; the menu's operations currently use software
under the strict capability restrictions. The previous 5.45 FPS Downloads image
is unchanged. Recovering speed now requires tested texture acceleration, not
claiming that this fill-only strict baseline meets the goal.
