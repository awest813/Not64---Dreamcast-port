# Video compatibility and frame costs — 2026-09-24

## Qualified fills in the fast renderer

Ordinary unblended one-cycle GPU fills now use the strict renderer's exact
quantization and coverage union in both builds. An isolated fill batch imports
no framebuffer texture, and reads back only pixels that its rectangles wrote.
Sparse readback skips empty 32-pixel mask groups. The mask adds 9,596 bytes to
fast-mode scratch (9,600 instead of the previous four-byte placeholder), within
the existing checked allocation budget.

Changing between exact fills and general textured/blended work completes the
previous batch first. These extra submissions preserve ordering and keep general
readback from accidentally using a sparse mask. A zero-alpha fill also completes
a pending exact batch before testing destination alpha; untouched pixels cannot
be assumed to have alpha one. GPU framebuffer dithering is disabled, avoiding
an additional color conversion on the already quantized N64 image.

Strict eligibility is unchanged. General fast textures and blended/fill-cycle
operations retain their existing accuracy limitations. This does not qualify
all fast rendering or emulate the N64's complete VI/dither pipeline.

## Validation

Final synthetic replays use production code and no ROM data:

- Fast now passes disjoint-fill RGB/alpha preservation, the 512-fill color ramp,
  and GPU/software/GPU transitions; these three fixtures failed before.
- Added fill/texture/fill ordering plus pending-fill/transparent-fill coverage.
  The fast run completes five GPU scenes in that fixture.
- The final-row/final-mask edge is covered by the disjoint-fill fixture.
- Host replay and the full high-resolution ARM32 regression suite pass.
- Strict passes the full exact replay checker with GPU execution established.
- Exact fast texture/state comparisons remain failures; the checker is not
  relaxed to hide them. Strict textures continue to use software fallback.

Private logs under `build/dc/validation/`: `video-fill-host.log`,
`video-fill-host-regression.log`, `video-fill-fast.log`, `video-fill-strict.log`,
and the corresponding build logs. Both local Stadium 2 images are rebuilt.

## Remaining performance work

Paired runs use the same genuine battle-command checkpoint, empty replay,
Flycast 2.6/OpenGL settings and first 60 presented frames (30 display lists).
Both benchmark windows run hidden. Times come from the emulated KOS clock:

| Metric | Before | After |
| --- | ---: | ---: |
| Total seconds | 55.175935 | 55.175705 |
| Effective FPS | 1.0874 | 1.0874 |
| Raster seconds | 40.172537 | 40.171867 |
| VI conversion seconds | 0.999064 | 0.999067 |
| Presentation seconds | 0.003447 | 0.003457 |
| PVR submissions | 60 | 60 |
| Texture hits / misses | 7,581 / 39 | 7,581 / 39 |

This scene's measured workload is effectively unchanged; no FPS improvement is
claimed. PVR reports only 0.139702 s in readback and 0.267733 s in framebuffer
import before the change. Copy reduction alone cannot reach 5 FPS here. These
are single, startup-inclusive checkpoint intervals, not repeated warm benchmarks
or physical Dreamcast measurements. Private evidence:
`video-fill-before-game.log` and `video-fill-after-game.log`. The preserved
pre-change image is `build/dc/not64-stadium-before-exact-fills.cdi`.

Next, profile the software fallback categories on the same battle checkpoint,
then qualify one common texture/combiner case before enabling more GPU work.
Keep opaque texture rounding, untouched-pixel preservation for general batches,
and post-draw combiner state as separate exact tests. Do not weaken their
expectations to obtain a passing fast result.
