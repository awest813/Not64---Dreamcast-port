# Progress checkpoint — 2026-09-21

## Current milestone: Stadium 2 gameplay

The fast Dreamcast build reaches the live battle command menu in Flycast 2.6:
Arcanine versus Nelson's Caterpie, Poké Cup / Poké Ball / Battle 1. The fast
build accepts C-up and visibly executes "Arcanine's Flame Wheel!". Target
validation uses genuine checkpoints produced by normal software emulation;
uninterrupted cold-boot-to-battle validation remains outstanding.

Implemented optional 640x480 field-weave output, two-cycle shaded depth
triangles, flipped texture rectangles and recorded controller input. Fixed a
fill/scissor off-by-one that overwrote the next framebuffer descriptor and
crashed the battle transition. Bounds checks stay enabled. The host completes
a 600-frame attack with HP damage and zero graphics/VI failures. Both default
and high-resolution regression suites pass, and both KOS fast images build.

See [Stadium 2 evidence, controls and limitations](tools/dc/STADIUM2.md).
Private launcher: `build/dc/Play-Stadium2-Fast.cmd`; private cold-boot image:
`build/dc/not64-stadium-fast.cdi`. No game assets are tracked. The earlier Zelda
Downloads launcher remains unchanged. **Compatible 5 FPS is still unfinished.**

## Previous OOT performance checkpoint

The exact software sampler improvements are implemented and validated. The measurements below are retained from the previous checkpoint.
The target remains **5 FPS with better compatibility**; it is not achieved yet.

Strict-mode OOT file selection improved from **0.516 to 0.586 FPS**, about
**14% more throughput**, without reducing resolution, changing filtering,
dropping geometry, or changing the emulated game clock.

| Strict menu build | Warm interval, frames 60–120 | FPS |
| --- | ---: | ---: |
| Previous exact rectangle-span checkpoint | 116.322003 s | 0.516 |
| Decoded-sample cache | 106.871118 s | 0.561 |
| Cache plus exact positive-coordinate floor | 102.413547 s | 0.586 |

These are individual warm intervals in isolated Flycast 2.6, using OpenGL and
render-to-texture readback, with profiling disabled. They are not repeated
gameplay qualification or physical Dreamcast measurements. AICA initializes at
32006 Hz; continuous audio quality has not been established.

## Completed in this checkpoint

- Added `SOFT_PROFILE=1` to measure RDP draw categories every 60 display lists.
  The host menu profile attributes approximately 86% of raster time to textured
  triangles, split between one-cycle and two-cycle draws.
- Added an 8 KiB decoded-texel cache. Texture uploads, palette uploads, LUT
  changes and tile/size changes invalidate it. Generation wrap and partially
  valid uploads are covered by tests.
- Replaced the floor operation for positive coordinates below 2^23 with exact
  integer truncation. Negative, zero, large and non-finite inputs retain libm.
- Preserved filter weights, accumulation order, combiner/blender state and
  framebuffer behavior. No new GPU rendering capabilities were enabled.
- Expanded replay coverage with 64 sample mutation/boundary cases, alongside
  the existing 96 framebuffer/depth/state comparisons and GPU fill fixtures.

## Validation

- Full ARM32 CPU and software graphics regression suite passes.
- Optimized ARM replay matches the ARM reference; optimized SH4 replay matches
  the SH4 reference, including the final strengthened stale-cache tests.
- The 60-frame menu image and complete saved machine state match the prior
  reference byte for byte. The opening-scene image also matches its reference.
- The checker rejects truncated output, changed state hashes and changed sample
  hashes. GPU scene counts still verify that fill fixtures exercise hardware.
- The optional profiler builds on both the host and KOS toolchains.

Near-boundary three-point arithmetic differs between ARM and SH4 even in the
unchanged sampler. Compare raw floating-point hashes against a reference built
for the **same CPU target**; no tolerance was added to hide this difference.

## Known limits and tradeoff

The nearest/clamped full-screen rectangle microbenchmark regresses from about
4.73 to 5.31 seconds, approximately 12%. Different raw coordinates which clamp
to the same texel still occupy separate cache keys. The OOT improvement does
not imply that every workload is faster.

Strict mode still accelerates only qualified opaque fills on the GPU. Most
textured work remains in software. Existing software parity is not proof of
complete N64 accuracy or broad game compatibility.

The Downloads launcher is unchanged. It still uses the experimental build with
the earlier approximately 5.45 FPS menu result and its documented correctness
limitations; the slower strict candidate has not replaced it.

## Resume here

The subsequent [dual-build audit and learning plan](tools/dc/DUAL_BUILD_LEARNING_PLAN.md)
sets the next priority: rebuild both modes from the same revision and localize
their draw-level differences before choosing another optimization. The list below
records the implementation checkpoint's remaining work.

1. Address the cache's nearest/clamped regression through measured admission or
   coordinate canonicalization, preserving exact wrap/mirror/clamp behavior.
2. Use the profiler to choose the next textured-triangle or blender optimization.
   Compare both menu performance and the regressed microbenchmark.
3. Enable additional GPU texture operations only after pixel/state replay proof.
4. Continue normal-boot, file-creation, gameplay, save/load and audio validation.
5. Apply the repeated 600-frame, multiple-scene acceptance criteria before
   claiming compatible 5 FPS.

## Local evidence and continuation references

Private validation artifacts remain outside version control:

- `build/dc/validation/floor-game.log`: final strict menu timing.
- `build/dc/validation/cache-game.log`: cache-only timing.
- `build/dc/validation/triangle-profile/`: draw attribution and host comparisons.
- `build/dc/validation/floor-{host,reference,kos,kos-reference}.log`: replay results.
- `build/dc/validation/floor-full-host.log`: full regression results.
- `build/dc/validation/floor-scenes/`: menu/opening captures and isolated saves.
- `build/dc/not64-oot-quality-floor.cdi`: private final strict test image.

See [replay instructions](tools/dc/RASTER_REPLAY.md),
[FPS quality plan](tools/dc/FPS_QUALITY_PLAN.md), and
[agent handoff](AGENT_HANDOFF.md) for build settings and remaining work.
