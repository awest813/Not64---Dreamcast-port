# FPS quality and compatibility audit

For the current cross-build experiment order, see the
[dual-build learning plan](DUAL_BUILD_LEARNING_PLAN.md). The initial audit below
and its appended implementation history remain the supporting record.

Audit: 2026-09-21, source revision `45a0ea0`. Scope: make the Dreamcast
hybrid renderer dependable before treating 5 FPS as a compatibility milestone.
This document proposes implementation; no renderer or launcher change was made
for this audit. Initial compatibility target is OOT through normal boot, file
creation and representative playable scenes. Other games need separate results.

## Finding

The existing 5.45 FPS result is real presented-VI throughput in a restored OOT
file-selection scene. It is not evidence of equivalent GPU pixels, full-game
compatibility, real-time audio, or physical Dreamcast performance. The fastest
path currently accepts operations whose semantics are only approximate.

Keep the proven CPU/VI improvements. Replace permissive GPU eligibility with
explicit, tested capabilities, measure the resulting slowdown, and recover
speed through batching, caching and exact software kernels. Do not preserve a
5 FPS headline by hiding unsupported draws or relaxing emulated timing.

## Measured budget

Local evidence: `build/dc/validation/fps5-installed-target.log`, frames 60 to
480, total-us 14,006,507 to 91,056,741. These are historical measurements, not a
new benchmark performed during this audit.

| Measured region | Mean per presented frame |
| --- | ---: |
| Whole interval | 183.45 ms (5.451 FPS) |
| Display-list processing, including hybrid rasterization | 70.63 ms |
| VI conversion | 13.62 ms |
| Presenter call | 0.06 ms |
| Remainder, not yet attributed | 99.14 ms |
| Headroom before 200 ms / 5 FPS | 16.55 ms |

The remainder includes interpreter, RSP audio work, scheduling and other work;
it must not be labelled pure CPU-interpreter time without instrumentation.
PVR waits/readback are nested in display-list processing; do not add them twice.
The tiny presenter call does not represent all asynchronous GPU completion cost.
`RASTER_PROFILE=0` makes the texture timing counter zero, not texture work free.
Frames and display lists match in this menu, but `Game perf` counts presented VI
updates (`platform/dc_gfx.c:198`), not independently measured game simulation ticks.
Track emulated elapsed time and VI/list/presentation counts separately in gameplay.

The older software menu was about 0.49 FPS. Reverting everything to software
cannot plausibly meet this goal through small optimizations alone. Conversely,
there is no measurement yet showing that correctness fixes fit the 16.55 ms margin.

## Audit findings, ordered by correctness impact

### 1. Entire-target alpha changes on partial GPU draws — confirmed

`PVRRaster::flush` (`platform/dc_raster_pvr.cpp:158`) reads back all 320x240
pixels and ORs alpha=1 into every RGBA5551 destination pixel. `begin` imports
only RGB565. Untouched pixels and alpha-tested holes therefore lose their
original alpha, unlike a software draw that only writes covered/passing pixels.
The zero-alpha fill skip also relies on this whole-target normalization.

Required: preserve untouched RDRAM exactly, including alpha. Choose a verified
alpha-preserving target/readback representation or an accurate coverage/write
mask; a bounding rectangle alone cannot preserve triangle holes and cutouts.
Until proven, restrict acceleration to cases that establish the entire output
state, or use software. Test a small draw over an alternating-alpha framebuffer,
alpha-test rejection, overlapping draws, and GPU/software/GPU transitions.

### 2. Filtering and combiner evaluation are reordered — confirmed

`texture` bakes combined texels, then `triangle` requests PVR bilinear sampling
(`platform/dc_raster_pvr.cpp:219`, `:301`). The software path samples/filters each
source first and then combines (`mupen64_soft_gfx/rs.cpp:103`). These operations
do not generally commute, especially two-texture products and clamping. In a
normalized example, filtering the baked product of correlated 0/1 textures at
the midpoint gives 0.5; multiplying the two filtered values gives 0.25.

There is also an independent kernel mismatch: any nonzero filter selects PVR
bilinear, while `TX::getTexel` has distinct three-point/average behavior. Merely
changing the texture format or increasing resolution cannot fix either issue.

Required: define a small proven-equivalent sampling/combiner subset. Keep
unsupported equations and fractional filtering in an exact software path until
an independently validated implementation exists. Test nonlinear two-cycle mux,
clamp boundaries, mirrored/wrapped tiles, different tile modes and fractional UVs.

### 3. Translucent texture precision and blend rounding — confirmed

Baked translucent colors use ARGB4444 (`platform/dc_raster_pvr.cpp:291`): all
channels, including alpha, drop to four bits. Opaque data uses RGB565. Blending
then happens in PVR before a further conversion to the N64 color representation.
This differs from the reference's channel precision and per-draw quantization.

Required: test alpha ramps, colored edges and multiple translucent layers against
both software and an independent reference. Use binary-alpha representations
only where the source and blend operation permit them. Do not assume an arbitrary
32-bit texture/RTT format exists on PVR. For general alpha, prefer an exact CPU
blend fallback unless a supported multipass representation proves equivalent.

### 4. GPU-to-software combiner state is incomplete — source-supported risk

Texture baking restores `CC` after a miss (`platform/dc_raster_pvr.cpp:274`),
and cache hits perform no per-pixel combiner evaluation. This avoids leaking the
last baked texel but does not reproduce the software draw's final `texel0`,
`texel1`, `combined` and related state. Rejecting COMBINED on the current GPU draw
cannot make a following software draw consume the correct prior state.
One-cycle TEXEL1 references are also not rejected; `combine1` does not update
TEXEL1, and that prior value is absent from the texture key.

Required: build two-draw reproductions before changing behavior. Specify which
state is actually architectural versus legacy software implementation state,
using independent reference results. Reject unproven dependencies, then make
cache hits and misses produce identical defined state. Do not simply add every
stale software temporary to the key and call that N64 compatibility.

### 5. Gameplay coverage is narrow — confirmed restriction, not a safe speed win

`eligible` rejects depth compare/update and non-320-wide RGBA16 targets;
`triangle` rejects partial scissor and varying shade. This is conservative, but
3D scenes may fall back frequently and repeatedly drain/import whole buffers.
The menu's zero fallback counters do not predict gameplay performance. Some
bounds/cycle rejections have no reason counter, so existing totals are incomplete.
Raw RDP lists are unsupported (`platform/dc_gfx.c:144`), and RDRAM remains 4 MiB.
The software blender itself assumes full coverage (`bl.cpp:204`); it is useful
for regression, not a complete correctness oracle. Existing menu seams may be
shared bugs in decoding/rasterization, not evidence of accurate PVR rendering.

Required: measure rejected pixels and transition costs as well as draw counts.
Implement only capabilities required by observed scenes, with depth/coverage and
ordering tests. Expansion-memory titles and additional microcodes are separate
compatibility work, not covered by this OOT milestone.

### 6. Audio and emulator dependence are insufficiently validated

`read_pcm` pads underruns with silence (`gc_audio/audio-dc.c:67`), and the stream
callback reports the requested byte count. AICA startup proves initialization,
not continuous sound. At slow emulation rates, buffer starvation needs explicit
measurement; smooth real-time audio cannot be promised merely because rendering
reaches 5 presented frames/sec.

Flycast 2.6 is the local validated configuration. A readback diagnostic failed
in 2.7 during prior work. Keep the pinned setup for repeatability, but verify
current emulator behavior and actual hardware separately before portability claims.

## Changes worth retaining

- Paired VI conversion: exhaustive 65,536-input conversion test plus scanout tests.
- Pure-interpreter direct operand views: full host suite and exact 60-frame saved
  state comparison. Extend to delay slots, exceptions, TLB and self-modifying code
  before calling the optimization broadly validated.
- CRC nibble table: preserves checkpoint format and known CRC vector; helps load
  time rather than steady-state FPS.
- Constant one-cycle fill combining: per-pixel blend/dither regression exists.
- Bounded cache and flush-before-TMEM-read/software fallback ordering: keep these
  safeguards while adding adversarial overlap, eviction and reset tests.
- Pixel-aligned alpha-test specialization: integer/unit-step checks are useful,
  but its precision and framebuffer-alpha behavior still need the tests above.

## Implementation plan and exit criteria

| Order | Work | Evidence required to advance |
| --- | --- | --- |
| Q0 | Build deterministic replay/capture and timing harness | Same commands, initial state and input schedule replay identically; comparisons locate the first divergent draw |
| Q1 | Add an opt-in strict compatibility mode and fix state/alpha handling | Untouched pixels, cutouts and cross-backend state tests pass; every rejection has a reason; no silent unsupported draw |
| Q2 | Recover speed with exact CPU kernels and cache/batch improvements | Bit/state equivalence tests pass and paired target benchmarks show a gain with strict mode enabled |
| Q3 | Restore only GPU sampling/blending capabilities proven correct | Each capability has software and independent-reference fixtures, including cache-hit/miss and mixed-path cases |
| Q4 | Expand through normal boot, file creation and playable scenes | Menu, indoor, outdoor, depth overlap, effects/transparency, pause and scene transitions validated; saves reload |
| Q5 | Qualify the release configuration | Repeated >=5 FPS measurements on each declared scene, quality checks, audio report, emulator/hardware support matrix |

### Q0: first implementation slice

1. Add a bounded synthetic display-list replay fixture, with initial RDRAM/TMEM,
   tile/mux/blender state and expected post-draw state. Keep game-derived captures
   local; commit only synthetic inputs and metrics.
2. Capture actual post-readback framebuffer words on the KOS target, not just a
   screenshot. Compare RGB and alpha separately, before VI scaling. Compare cache
   cold/hot runs and force software/GPU alternation. Host tests do not exercise PVR.
3. Add the alpha-preservation, filter/combiner-order, alpha-ramp and two-draw state
   cases above. Use an independent renderer or analytically specified synthetic
   expectations to distinguish shared software bugs from PVR regressions.
4. Extend existing phase timers with sampled interpreter/RSP-audio attribution,
   all fallback reasons, flush/import counts, pixels processed, audio produced /
   consumed / silence bytes, and frame-time distributions. Measure profiler cost.
5. Capture normal-boot and playable-scene checkpoints using the owned ROM. Existing
   slots 7/8 cover menu/opening only. Use deterministic input for comparisons;
   checkpoints supplement normal-boot validation rather than replacing it.

Q0 delivers the facts needed to price correctness fixes. Then Q1 implements a
strict capability table, separate from the current experimental mode. Expect an
initial FPS regression; record it instead of weakening the tests.

### Q2: performance work that does not require visual shortcuts

Profile first; the 99.14 ms remainder is the largest unattributed region.
Candidates, not promised savings:

- Hoist invariant clipping, address validation and blend selection out of safe
  software spans; retain slow paths at boundaries. Specialize common mux/blend
  combinations with bit-exact channel and rounding tests before replacing floats.
- Track TMEM/palette mutations and immutable draw state so unchanged textures do
  not require repeated full-key copying/hashing. Preserve overlap/coherency checks
  and collision-safe comparison; test partial writes and palette-only updates.
- Batch consecutive compatible draws in original order. Bound readback/import to
  correctly tracked writes only after Q1; avoid shortcuts that discard alpha.
- Audit the VI conversion and copy path against the 13.62 ms target cost. Retain
  stride, word-swap, crop, odd-width and bounds behavior; benchmark DMA/store-queue
  alternatives on the target before adopting them.
- Optimize interpreter dispatch/decoding only with instruction/state differential
  coverage. If these changes cannot fund strict rendering, consider a separately
  scoped SH4 CPU backend project; do not substitute altered emulated timing.

Seek roughly 30-40 ms/frame of measured savings to provide quality headroom.
That is a planning target, not an estimated or guaranteed win. With the current
menu at 183.45 ms, a 5 FPS-compatible gameplay renderer remains unproven.

### Q5: acceptance contract

- Native emulated resolution, normal game clock/interrupt behavior, no dropped
  geometry, synthetic blank frames, lowered VI rate or resolution reduction.
- For every claimed scene: three runs of at least 600 presented VI frames after
  a disclosed warmup; >=5 FPS mean in each run. Report 60-frame window rates,
  median/p95/max frame time, emulated-time progress, list count and cold-cache cost.
  Do not replace a failing scene with a menu average or emulator display refresh.
- Exact framebuffer/state parity for capabilities claimed equivalent. For known
  reference differences, require reviewed independent evidence and an explicit
  per-feature tolerance; never waive alpha/depth/state bugs with a global image score.
- Zero decode/presentation failures; input, pause, scene changes and save/load work.
  Report unsupported features explicitly. No full compatibility claim from OOT alone.
- Report audio starvation and listen to a captured run; keep audio timing intact.
  State separately whether sound is merely functional or continuously acceptable.
- Log compiler/KOS version, emulator/backend/settings, ROM identity privately,
  memory/VRAM high-water marks and build revision. Hardware validation is required
  for a physical Dreamcast performance claim.

Recommended next action: Q0 and the small alpha-preservation/state reproductions,
then Q1. Do not add broad GPU depth/filter approximations before this foundation.

## Implementation progress: first Q0/Q1 slice

The synthetic RDP replay and opt-in `RASTER_STRICT=1` baseline are implemented.
The same fixtures run against the ARM32 software reference and actual KOS/PVR
readback. Strict mode passes disjoint-fill alpha/RGB preservation, all-channel
opaque ramps crossing a submission limit, a mixed CPU/GPU transition, and the
software COMBINED dependency cases. GPU scene counters prevent all-software
execution from being mistaken for an accelerated pass. The experimental mode
fails the negative control, confirming these tests detect existing problems.

Strict mode enables only opaque one-cycle GPU fills, with an exact write mask
and explicit color quantization. Other operations fall back to software. This
is the start of the capability table, not completion of Q0/Q1 or a renewed 5 FPS
milestone. Full trace replay, broader state/filter fixtures, audio counters and
normal-boot/gameplay coverage remain outstanding. See [replay details](RASTER_REPLAY.md).

Measured strict menu baseline: 0.516 FPS over frames 60-120 (116.317821 s),
with AICA initialized. The remaining speed gap is explicit; the 5.45 FPS
experimental result does not transfer to strict mode. Next: reference-tested
texture sampling/combiner fixtures and a narrowly qualified texture path.

## Exact rectangle span follow-up

An exact software opaque one-cycle texture-rectangle path now hoists bounds and
blend selection out of the pixel loop. It retains sampling, filtering, combiner
state and final blender state; unsupported cases use the prior loop. ARM32 and
KOS replay comparisons cover 96 framebuffer/depth/boundary/state cases, and the
full host regression suite passes. The 60-frame OOT menu capture and complete
saved state match the prior reference byte for byte.

The isolated target rectangle workload is 1.78x faster (24 full-screen draws:
8.40 -> 4.73 seconds), but the strict OOT warm sample remains 0.516 FPS
(116.322003 seconds for frames 60-120). Do not project the microbenchmark gain
onto the game. Next: attribute the textured-triangle cost, extend exact span
qualification to the dominant cases, and retain software for unproven GPU
filtering/quantization. No new GPU capability or Downloads launcher change.

## Triangle profile and exact sampler work

`SOFT_PROFILE=1` now prints cumulative RDP call counts and elapsed microseconds
by draw category and cycle every 60 display lists. It works with the host or
KOS software renderer. Keep it off for final FPS measurements. The counters
measure time inside each RDP call; GPU flushes can move deferred GPU time into
the next call, so use software runs to attribute software work.

The initial ARM32 menu profile (`triangle-profile/menu.log`) attributes 12.417 s
of 14.453 s raster time to textured triangles: 6,240 one-cycle calls take 5.516 s
and 3,000 two-cycle calls take 6.901 s. This establishes the next bottleneck;
opaque rectangles alone were not representative of the scene.

A 256-entry decoded-sample cache retains raw floating-point texel colors, keyed
by tile and integer coordinates. Its 8 KiB of entries live with the renderer.
TMEM uploads, palette uploads, LUT changes and tile/size changes invalidate the
generation; wrap clears old tags. Partial uploads invalidate before writing.
Filter weights, accumulation order, combiner operations and game timing remain
unchanged. A positive bounded coordinate floor uses exact integer truncation;
other coordinates retain the original libm operation.

The replay adds 64 texture mutation cases, format/palette changes, collisions,
wrap, partial uploads and fractional/large coordinate boundaries. ARM optimized
and ARM reference match; SH4 optimized and SH4 reference match. Near-boundary
three-point arithmetic differs between those CPU targets even in the original
sampler, so same-target comparison is required for raw-float hashes. This is
not a new tolerance or evidence of independent N64 accuracy.

Cache-only target evidence (`cache-game.log`): frames 60-120 take 106.871118 s,
versus 116.322003 s before this work: 0.561 FPS versus 0.516 FPS. This is one
warm interval in Flycast 2.6, with profiling disabled; it is not a 5 FPS result.
Full CPU/software regressions and menu/opening reference captures remain part
of the final validation, rather than projecting host or microbenchmark speed.

Final cache-plus-floor target evidence (`floor-game.log`): frame 60 is at
102,888,654 us and frame 120 at 205,302,201 us. The warm interval is
102.413547 s, or **0.586 FPS**, a **13.6% throughput gain** over the prior
0.516 FPS strict baseline. Native resolution, filtering, geometry and game timing
are unchanged; AICA starts at 32006 Hz. This remains one menu interval, not the
multi-scene/repeated-run Q5 acceptance result. Downloads remains on its existing
experimental image. The full-screen nearest/clamped microbenchmark regresses
about 12%; cache admission/canonicalization is a follow-up candidate, and must
not be confused with the measured game improvement.
