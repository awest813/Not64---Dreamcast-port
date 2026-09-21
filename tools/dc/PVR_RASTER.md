# Experimental PVR geometry backend

See [FPS quality and compatibility audit](FPS_QUALITY_PLAN.md) for the
correctness findings and staged plan beyond the menu throughput milestone.

The opt-in `RASTER_STRICT=1` baseline accelerates only tested opaque fills.
See [synthetic replay instructions and results](RASTER_REPLAY.md). The existing
5.45 FPS result applies to experimental mode, not strict mode.

`RASTER_PVR=1` adds selective hardware rasterization to `GFX=soft VIDEO=pvr`.
It is opt-in and requires KallistiOS with `pvr_scene_begin_rtt`. The default
software renderer remains available as the reference and fallback.

```sh
make -f Makefile.dc VIDEO=pvr GFX=soft RASTER_PVR=1 INTERP_DIRECT=1 \
  GAME_DISC=1 GAME_CHECKPOINT=1 GAME_FRAMES=0 GAME_STEPS=0 GAME_START=0 \
  PERF=1 LTO=1 -j8
```

`INTERP_DIRECT=1` avoids the temporary operand-decode structure in the pure
interpreter. It does not generate native code. Host checks compare complete
saved CPU, memory and device state after the same 60-frame Zelda menu run.

`RASTER_PROFILE=1` enables two timer reads per texture request. Leave it off
for throughput measurements; `PERF=1` still records every game-frame interval.
The texture cost counter is zero when detailed profiling is disabled.

The geometry path supports a bounded subset of RGBA16, 320-pixel-wide color
buffers: opaque/standard-alpha triangles, constant fills and texture rectangles.
It bakes supported combiner equations into cached PVR textures. Unsupported
draws drain the GPU and continue in software. Fixed alpha tests are baked only
for integer-aligned, unit-step, unshifted texture rectangles using standard
source-alpha blending; these use nearest sampling to preserve the selected
texel. General filtered or dithered alpha tests remain in software.
TMEM uploads that read an active
color target also drain it before reading the exact source range. The end of
each display list returns the color buffer to N64 RDRAM.

Main-RAM keys and staging have a 1,280 KiB budget, enforced by a static assertion.
The cache has 128 entries and a 2 MiB VRAM limit. Two 256 KiB render/import
textures are separate from the presenter's two 256 KiB scanout textures. Eviction
waits for pending draws before freeing textures.

This is not a cycle-accurate RDP implementation. PVR bilinear filtering and
ARGB4444 texture alpha differ from the reference's three-point filter and
eight-bit alpha. Coverage, cross-draw combiner feedback, general depth rendering
and full gameplay compatibility are not established. The validated target is
Zelda's file-selection checkpoint at native resolution; no frames are skipped.

For validation on this machine, use an isolated official Flycast 2.6 installation
with OpenGL and `rend.RenderToTextureBuffer=yes`. Flycast 2.7 returned zero pixels
in the isolated readback diagnostic (and crashed with DirectX); the same test
returned the expected red RGB565 pixels in 2.6. Normal PVR presentation works
with `rend.EmulateFramebuffer=no`.

Measure `Game perf` frame deltas against `total-us` deltas. PVR submission counts
and Flycast's displayed FPS are not independent proof of N64 frame rate. Private
ROMs, checkpoints, disc images and validation captures stay outside version control.

## Validated menu measurement (2026-09-21)

With `PERF=1 LTO=1`, detailed texture profiling off, and the flags above,
`fps5-pvr14-target.log` records frame 60 at 14,006,507 us and frame 420 at
80,384,361 us: **360 / 66.377854 = 5.423 FPS**. There is one completed display
list and one rendered/read-back scene per game frame. AICA runs at 32006 Hz.
The visible 320x237 VI menu retains File 1–3, Copy, Erase, Options and the footer.
Existing horizontal menu seams remain; this is not an accuracy milestone.

Validation also includes the complete ARM32 host suite with and without
`INTERP_DIRECT`, an exhaustive RGBA5551-to-RGB565 conversion test, and exact
60-frame software capture/checkpoint comparisons with the reference. The
software capture SHA-256 is
`1fea9e40ea278ff3e2307d9c2b584ec827da42f93ce60c0e24f3190a46371a2a`.

The local Downloads shortcut uses `not64-oot-5fps.cdi`, byte-identical to the
measured prototype14 image (SHA-256
`d41709d9916ef269230609a3ad2f2267b9979bac32373f7a0fc1e4362e5dd76a`).
The old software launcher is retained separately. Both checkpoint launchers
restore file selection on startup; they do not resume later gameplay.
