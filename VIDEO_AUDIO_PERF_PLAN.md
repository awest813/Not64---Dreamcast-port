# Video and audio performance: audit and plan

Audit date: 2026-09-20. Repository revision: `e5aa1c9` (after the VI/present
kernel polish and the DMA invalidate-sweep fix). Scope: where frame time and
audio CPU time actually go on the Dreamcast configuration (pure interpreter,
200 MHz SH4, 4 MiB RDRAM, PVR textured-quad presenter, no AICA output yet),
and the ordered plan to reduce both. This is a plan, not an implementation;
nothing here marks a phase done.

## What runs per emulated frame today

```
VI interrupt (≈60 Hz emulated, inside r4300/interupt.c gen_interupt):
  updateScreen()                       platform/dc_gfx.c
    dc_vi_convert()                    platform/dc_vi.c   — RDRAM → RGB565 512x256
    dc_video_present()                 platform/dc_pvr.c  — wait, upload, submit
  (with GFX=soft, before that: dc_soft_dlist → mupen64_soft_gfx into RDRAM)

AI DMA (per audio buffer, every ≈16–32 ms game time):
  aiLenChanged → ring memcpy           gc_audio/audio-dc.c
  doRspCycles(100) → rsp_hle alist DSP rsp_hle/        (per audio task)
```

Measured / instrumented costs:

| Path | Cost | Evidence |
|---|---|---|
| VI convert, 320x240 16bpp | ≈91 us/frame at `-O2` on the ILP32 regression build, 1.85x faster than pre-polish | QEMU A/B bench, 2026-09-20 (ratio transfers; SH4 cycles not yet measured) |
| VI convert clear | row tails + bottom rows only (was full 256 KiB every frame) | commit `c0b99ef` |
| PVR texture upload | 240 KiB/frame (used rows), synchronous `pvr_txr_load` | `dc_video_pvr_upload_bytes` |
| PVR present | `pvr_idle()` blocks on TA drain + render done **every frame**, serializing emulation behind the previous render | `platform/dc_pvr.c:77`; wait/upload/submit averages print under `DC_EMBED_VITEST` (`Makefile.dc:169`) |
| Soft renderer | per-pixel `Color32` float math, `validPixel()` twice per pixel, `zLUT` encode per pixel | `mupen64_soft_gfx/bl.cpp:194–232` |
| Audio HLE | per-sample loops with `^S16`-phase halfword loads (`rsp_hle/memory.c:36–67`), per-sample volume ramps in envmix (`rsp_hle/alist.c:404–430`) | source audit; unmeasured |
| Audio output | **none.** The ring drains only when full; `AiUpdate` is never called (`main/plugin.c:399–435` pointers commented out); AICA/`snd_stream` unwired (P2 leftover) | source audit |
| Unused settings scaffold | `vilimit` exists in `dc_settings.c:52` but no consumer; `count_per_op` not exposed | source audit |

Rough magnitude ordering for a game frame (interpreter ≫ soft raster ≫
present path ≫ audio HLE ≫ AI copy). Two consequences drive the plan:

1. Gameplay video cost is dominated by emulation throughput plus the software
   rasterizer; the present path is small but **serialized** — the emulator
   sleeps on the PVR every frame, so its wall-clock share is larger than its
   CPU share.
2. Audio has no audible output yet, so "audio performance" is first a
   functional gap (AICA), then a CPU-cost problem in `rsp_hle`.

## Plan

Ordered by (win x safety); each phase lands with the same validation pattern
the VI work used: host ILP32 suite + differential fuzz against the old code,
plus target evidence where the toolchain allows.

### V1 — Unserialize the PVR present (largest video wall-clock win) — DONE 2026-09-20

Shipped as ping-pong textures + double vertex buffer + DMA upload:
two 256 KiB textures alternate (512 KiB VRAM total, stable across reopen
cycles), `vbuf_doublebuf_disabled` was removed so `pvr_scene_begin` no longer
waits render-done internally, the per-frame `pvr_wait_render_done` is gone
(shutdown still drains both), and `pvr_txr_load_dma` hands the texture copy to
the G2 DMA engine with a store-queue fallback. Safety: KOS clears what
`pvr_wait_ready` waits on only when the submitted scene's render *starts*, and
renders are sequential — so by upload time the render that read the texture
being overwritten has finished.

Flycast-measured (DreamSDK GCC 15.1 `-m4-single` build, emulated SH4 µs/frame,
272-frame demo): upload **618 → 30**, submit 14–15 unchanged, wait 0 in both
(Flycast's PVR model completes renders instantly, so the de-serialization is
a real-hardware win the emulator cannot show). Present-path total
≈632 → ≈45 µs/frame. `check_target_log.py` passes the full run, now expecting
`texture=524288` and VRAM-free 6,281,896. Evidence:
`build/dc/validation/` serial captures; Windows capture tooling:
`tools/dc/scrape_console.ps1` (attach + read the emulator's serial console)
and `tools/dc/shot_flycast.ps1` (window screenshot).

### V2 — Skip redundant texture uploads

Menus, pauses, and static scenes re-submit identical frames at 60 Hz. Keep a
rolling checksum (the tree already carries `xxhash`/`adler32`) over the
converted rows; when unchanged since the last present, skip `pvr_txr_load`
but still submit the quad so the PVR's background clear keeps the image on
screen.

- Expected: 0 uploads in static scenes (up to 240 KiB/frame saved), one
  hash pass (~240 KiB sequential reads) in their place.
- Touches: `platform/dc_gfx.c` (frame state), `platform/dc_pvr.c` (skip flag).
- Risk: low; exact-capture tests unaffected (conversion still runs).

### V3 — Frame-phase profiling as a diagnostic

`DC_EMBED_VITEST` already times wait/upload/submit, but only in demo builds.
Generalize: a debug setting that accumulates interp/raster/convert/upload/
present microseconds and prints one summary line on ROM close (reuses
`timer_us_gettime64` + `dc_log`). Every later phase is then driven by
measurements instead of source reading.

### V4 — Soft renderer span work (biggest gameplay video CPU win)

Profile first (V3), then in this order:

1. **Hoist `validPixel` to span level** — it is called twice per pixel
   (`cImg` + `zImg`) and re-derives base/ram/width every time
   (`bl.cpp:255–262`). Spans already know x/y bounds; check once per span.
2. **Copy-mode 32-bit writes** — `copyModeDraw` stores one halfword per pixel
   with `y*width+x^S16` addressing; pixel pairs are contiguous, so one
   aligned 32-bit store covers two pixels (same trick as the VI converter
   and `expand_2x`).
3. **Fixed-point Color32 for the cycle-1/copy paths** — the per-pixel float
   multiply/round in `Color32` is the top op count; replace with integer
   5-bit math where the RDP combine semantics allow. Behind golden-image
   tests: `dc_gfx_capture_ppm` + `tests/dc/check_capture.py` already prove
   pixel-exact output, so each step lands with a before/after capture.

Risk: moderate; this is upstream algorithm code, so every change must be
gated on the golden-image captures, not eyeballing.

### V5 — SH4 micro-tuning of VI convert (only if V3 still shows it)

Unroll to four pixels per iteration with two loads, add row-ahead
`__builtin_prefetch`, keep `-O2`. Diminishing returns after the 1.85x round;
do not start here.

### A1 — Wire AICA output via snd_stream (prerequisite for audible anything)

The KOS kernel ships `kernel/arch/dreamcast/sound/snd_stream.c`. Wire it:
`AiDacrateChanged` picks the stream rate; `AiUpdate` (currently dead — see
`main/plugin.c:399–435`) becomes the consumer that pushes ring bytes with
`snd_stream_push` instead of discarding them; re-hook the plugin pointer.
This is functional completion, but it also establishes real audio pacing
(the ring drains at consumption rate instead of the current
drain-when-full behavior), which A2's measurements need.

- Touches: `gc_audio/audio-dc.c`, `main/plugin.c`, `Makefile.dc` (no extra
  lib — snd_stream is in libkos).
- Risk: low; the ring-buffer shape already matches `snd_stream_push`.
- Validation: host ring-drain unit additions + Flycast serial
  (`audio ring buffered …` lines) + audible check on hardware.

### A2 — rsp_hle sample-path bulk work

After A1 makes audio cost measurable:

1. `load_u16`/`store_u16` (`rsp_hle/memory.c:36–67`) walk one halfword per
   iteration with alternating `^S16` phase. On aligned runs the phase folds
   into a 32-bit load that yields two samples (identical to the VI converter's
   pair trick). These hit every ADPCM/resample buffer DMA in and out of DMEM.
2. Envmix/resample inner loops recompute gains and walk four buffers per
   sample; hoist the ramp step and gain math, and let the `^S` pointer xor
   collapse into pair iteration.

Validate exactly like the VI work: differential fuzz of old vs new helpers
over random buffers + the existing audio smokes. Risk: low-moderate.

### A3 — Headroom levers (settings, not code paths)

- Expose `count_per_op` (interpreter cycles-per-op; the standard Wii64 speed
  knob) — raises emulation throughput for everything, at a documented
  accuracy cost.
- Wire the existing but unconsumed `vilimit` setting (Off / Wait for VI /
  Wait for frame) in the main loop.
- Optional frameskip-in-VI (present every Nth emulated frame) as an explicit
  user setting; combine with V2 so skipped frames cost only the hash.

### Explicitly out of scope (locked)

SH4 dynarec, TA/RDP geometry acceleration (gated behind the PVR_PLAN stages
and a broader game set), Expansion Pak, and any LP64 host relaxation. The
biggest possible lever — the dynarec — stays locked until a human revisits
`AGENT_HANDOFF.md` "Locked".

### Prerequisite for target numbers on this machine — RESOLVED 2026-09-20

DreamSDK (`C:\DreamSDK`) provides the validated configuration directly:
sh-elf **GCC 15.1.0** with KOS 2.2-era headers and libs defaulting to
`-m4-single` (64-bit double) — `dc_contract.c` compiles and the demo ELFs
build and run. Build via DreamSDK's MSYS2:

```sh
/c/DreamSDK/usr/bin/bash.exe -lc 'source /opt/toolchains/dc/kos/environ.sh \
  >/dev/null 2>&1; cd /f/GitHub/Not64---Dreamcast-port && \
  make -f Makefile.dc VIDEO=pvr DEMO=1 -j8'
```

Flycast 2.7 (`%USERPROFILE%\Downloads\flycast-win64-2.7`) runs the ELF; serial
output goes to its own console window — capture with
`tools/dc/scrape_console.ps1` (resize early to 220 columns so long lines do
not wrap, read the buffer after the run). The einsteinx2 Docker image remains
`-m4-single-only`-only and stays unusable for full links.
