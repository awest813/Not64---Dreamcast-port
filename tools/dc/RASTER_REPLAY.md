# Synthetic PVR correctness replay

The replay uses the production RDP, software rasterizer and (on KOS) PVR
backend. It contains synthetic data only and needs no game ROM. It checks
post-readback RGBA5551 words before VI conversion, not screenshots.

## Run the reference

From the repository root in the existing ARM32 Docker build environment:

```sh
make -f tests/dc/Makefile.raster-replay HOST=1 REFERENCE=1
qemu-arm -L /usr/arm-linux-gnueabihf build/dc/raster-replay-1-0-reference/replay.elf > reference.log
python3 tools/dc/check_raster_replay.py reference.log
make -f tests/dc/Makefile.raster-replay HOST=1
qemu-arm -L /usr/arm-linux-gnueabihf build/dc/raster-replay-1-0/replay.elf > optimized.log
python3 tools/dc/check_raster_replay.py optimized.log --reference reference.log
```

## Run the actual GPU path

With the current KOS environment loaded, from the repository root:

```sh
make -f tests/dc/Makefile.raster-replay STRICT=1
make -f tests/dc/Makefile.raster-replay SOFTWARE_ONLY=1 REFERENCE=1
```

Boot `build/dc/raster-replay-0-1/replay.elf` in the isolated Flycast 2.6 setup,
using OpenGL, serial console enabled, `rend.RenderToTextureBuffer=yes`, and
`rend.EmulateFramebuffer=no`. Capture that process's console using
`tools/dc/scrape_console.ps1`, then validate:

```sh
python3 tools/dc/check_raster_replay.py --gpu strict.log --reference target-reference.log
python3 tools/dc/check_raster_replay.py --software-only target-reference.log
```

The target prints one completion summary and stays idle so serial results can
be captured. Close that test instance after collection. The checker rejects
missing, repeated, truncated or failing fixture results. `--gpu` also requires
completed scenes for every fixture that is supposed to exercise hardware.

Capture `target-reference.log` from `raster-replay-0-0-reference-software/replay.elf` with
the same emulator settings. Use the same CPU target for the exact floating-point
sample comparisons: the near-boundary three-point cases expose differences
between ARM and SH4 even in the unchanged sampler. The checker deliberately
rejects those differences; do not introduce a tolerance to hide them.

`REFERENCE=1` disables the software optimizations; `SOFTWARE_ONLY=1` also
excludes the PVR rasterizer from the target binary. Both are necessary for the
unoptimized software-only SH4 reference. Every new replay reports its backend.
The checker rejects old logs without that identity, GPU reference logs, and
cross-CPU reference comparisons. Rebuild and recapture instead of relabeling logs.

For the negative control, build with `STRICT=0` and boot
`build/dc/raster-replay-0-0/replay.elf`. This mode currently fails the suite;
the checker must return nonzero. A successful build is not a successful replay.

## What is tested

| Fixture | Independent expectation / comparison |
| --- | --- |
| Framebuffer import | All 65,536 RGBA16 codes, paired colors, both halfword alignments and row padding |
| Native 640-pixel canvas (`WIDE=1`, checker `--wide`) | Every 640x480 word, untouched gaps/alpha, bottom/right edges, 320→640→320 RTT stride changes, regional texture import/readback, blended backdrop reads, odd edges, region reset and pending transparent fills; three strict or eight fast completed GPU scenes |
| Two disjoint opaque fills | All 76,800 framebuffer words: only covered pixels change; the gap retains RGB and alternating alpha |
| 512 opaque fills | Every input channel value, exact five-bit output; at least two GPU scenes exercise the submission limit |
| GPU/software/GPU transition | Rejected alpha-test samples preserve the destination; an intervening CPU edit survives the next batch |
| Two-cycle producer, one-cycle consumer | Matches the software combiner's defined prior-state behavior on cold and hot cache candidates |
| Opaque software texture rectangles | Analytic RGB/alpha ramp plus 96 reference comparisons of framebuffer, depth, boundary words and exact floating-point combiner/blender state |
| Decoded sample cache | 64 before/after mutation cases across RGBA, CI, IA and I formats, palette/TMEM/tile changes, collisions, epoch wrap and a partially valid upload; fractional and large coordinate boundaries |
| Cutout color precision | All 32 grayscale levels, cold/hot cache draws, transparent pixels and post-combiner fractional alpha; fast mode must complete 66 GPU scenes, exact framebuffer expectations remain mandatory |
| Fill/texture transitions | Exact fill, textured draw, exact fill in order; pending sparse fill followed by transparent blending must respect destination alpha |
| Second texture use | 64 exact software/reference comparisons cover both cycles, RGB/alpha sources, tile wrapping, rectangles, both triangle halves, depth rejection and subsequent TEXEL1 consumption |
| Unused next tile | Analytic uniform-red textured draw with a mismatched unused next descriptor; fast must complete a GPU scene |
| Experimental varying shade | Six interior-color probes for separable opaque and translucent equations, varying depth, negative-bias fallback and cutout-alpha fallback; maximum two five-bit channel levels, exact alpha; strict remains software |

Build the optional shade replay with `AFFINE=1 STRICT=0`. Its default output
directory gains an `-affine` suffix; an explicit `OUT` must also stay separate
from other configurations. `--affine --gpu` requires four completed shade scenes.
The full fast replay remains a negative control and must fail its existing exact
texture/state groups. The bounded shade probe adds no tolerance to those groups.

2026-09-24: fast mode now shares the qualified one-cycle opaque fill path.
Disjoint fills, the color ramp, and mixed software/GPU fills pass in fast mode,
as does the new fill/texture transition fixture. The full fast replay still
fails texture/state tests. See [frame-cost notes](VIDEO_COMPAT_FRAMES.md).

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
word operations go through the software renderer; eligible opaque one-cycle texture
rectangles use the exact span optimization below. These GPU restrictions
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

## Exact software texture spans (2026-09-21)

Opaque one-cycle texture rectangles now validate bounds and select the identity
blender once per rectangle. They retain the original row/column order, floating
coordinate increments, sampler, filter and combiner. Color32 conversion supplies
the same channel clamping and RGBA5551 packing; final pixel/shade registers are
restored explicitly. Depth, active alpha comparisons, alpha-to-coverage,
destination reads, blending, empty rectangles and invalid bounds retain the
original per-pixel loop. Alpha-from-coverage remains supported and preserves its
final register value. There are no new GPU capabilities or timing shortcuts.

`REFERENCE=1` disables only this span optimization and selects a separate output
directory by default. The replay compares 96 cases covering fractional sampling,
point/three-point/average filters, scissoring, alpha, depth, blending and RDRAM
boundaries. Each case hashes framebuffer words, depth words, the end-of-RDRAM
guard region, and the raw floating-point combiner/blender registers. The initial
32x8 texture also has an analytic expected result. Always supply `--reference`
when validating optimized runs; the self-contained PASS lines alone do not
establish differential equivalence.

In isolated Flycast 2.6 strict builds, 24 full-screen texture rectangles took
8,402,565 us with the original loop and 4,727,047 us with the optimized loop:
43.7% less time (1.78x throughput). Both produced hash `d6ea0d85`. This is one
synthetic sample per build, not an OOT FPS claim or hardware measurement.
Evidence: `span-kos-reference.log`, `span-kos-benchmark.log`, and the corresponding ARM32
`span-reference.log` / `span-host.log` under ignored `build/dc/validation/`.

The existing full ARM32 suite passes (`span-full-host.log`). A 60-frame menu
capture and complete saved state also match the prior direct-interpreter
reference byte for byte (`span-menu/` versus `direct-opt/final.ppm` and its
saved slot 1). The Downloads build remains unchanged.

The strict OOT warm interval remains **0.516 FPS**: frames 60-120 take
116,322,003 us (`span-game.log`), versus 116,317,821 us before this change.
There is no meaningful menu improvement. The new rectangle path does not address
this scene's dominant work; the next performance work should profile and qualify
textured triangle spans, rather than extrapolating the synthetic rectangle gain.

## Decoded samples and coordinate floor

`REFERENCE=1` also disables the decoded-sample cache and positive-coordinate
floor optimization. The 64 `cache-case` records compare raw float colors before
and after mutations, in addition to the existing pixel/state replays. Run both
builds on the same CPU target when comparing near-boundary filter arithmetic.
The cache retains decoded colors only; no filter result or blend result is
reused. Its generation changes on every texture, palette, LUT or tile mutation,
including uploads which write a valid row before encountering invalid bounds.

Final sampler correctness evidence: `floor-host.log` versus `floor-reference.log`,
and `floor-kos.log` versus `floor-kos-reference.log`. Both pass all fixtures;
the cross-CPU boundary mismatch is retained as a failing control. Full host
regressions pass in `floor-full-host.log`. `floor-scenes/menu.ppm` and the saved
slot 1 match the prior menu image and complete state byte for byte;
`floor-scenes/opening.ppm` matches the established 60-frame opening reference.

The cache is not faster for every workload. The 24 full-screen nearest/clamped
rectangle benchmark takes 5,312,926 us with cache plus floor versus 4,727,047 us
with the preceding span-only implementation (about 12% slower). Raw coordinates
which clamp to the same edge texel still occupy different cache keys. Keep this
case in performance comparisons; the measured OOT gain does not establish a
general renderer speedup. Cache admission/canonicalization needs its own measured
change and equivalence checks before altering this policy.

With profiling disabled, the final strict OOT menu run improves to **0.586 FPS**:
60 frames / 102.413547 s (`floor-game.log`, frames 60-120), compared with
0.516 FPS / 116.322003 s in `span-game.log`. This is 13.6% more throughput
in one warm Flycast interval. The separate private image is
`not64-oot-quality-floor.cdi`; the existing Downloads launcher is unchanged.

## Stadium 2 optional capability probes (2026-09-30)

New fast capabilities remain opt-in: `IDENTITY=1`, `CUTOUT=1` (both with
`AFFINE=1`), `BATCH_CACHE=1`, `FULL_ALPHA=1`, `RECT_CLIP=1`, `RECT_SAMPLE=1`,
`TEXEL_SCALE=1` (requires rectangle sampling) and `CACHE_INDEX=1` (requires
batch cache), `TEXTURE_ALPHA=1` and `CVG_POINT=1` (both require full alpha).
Use separate `OUT` directories and pass the corresponding checker flags:
`--gpu --wide --affine --identity --cutout --batch-cache --full-alpha
--rect-clip --rect-sample --cache-index --texture-alpha --cvg-point`.
The checker requires actual completed GPU scenes,
complete case sequences, and the final zero-failure result. Focused probe PASS
lines cannot override a failing full replay.

`texture-mutations` covers15 texture/palette/LUT/load/reset cases;
`texture-dependencies` covers12 exact shader-input dependency changes;
`texture-batches` exercises300 textures, completed-batch eviction, pinned
flushes and both rectangle/triangle submission boundaries; `identity-blender`
and `full-alpha` cover exact identity output and current constant-alpha255
proofs with rejection controls. `texture-alpha` covers nine raw source-opacity,
constant-alpha, transparent, fractional-IA and unknown-source cases; the optional
capability must execute four new GPU scenes. `coverage-point` checks15 exact
whole-image cases: binary/fractional source alpha,31/32 thresholds, fixed alpha
comparison, unused hot state, fallback controls, destination alpha0 holes and
two overlapping masks in one pending batch. The optional path must execute11
new GPU scenes. It bakes accepted identity writes to alpha255 and limits readback
to their union; rejected pixels retain destination RGB and alpha.
`cutout-shade` covers cold/hot prim-color
changes, binary versus fractional alpha and the low-prim fallback.
`rectangle-scissor` checks clipped integer boundaries, empty rectangles and
fractional-origin fallback. `rectangle-sample` compares a linear filtered ramp
at software pixel origins against two GPU scenes, allowing at most one
five-bit channel step. Existing exact texture/combined/span checks stay exact.

The corrected packed-texel fast build passes combined-cold, combined-hot,
texture-precision and texture-spans, fixing the four historical groups. The
indexed build also passes four deliberate full-hash collision cases, including
cold and hot reuse of different red/blue TMEM contents. Genuine SH4 software-only
and strict replay pass. Current complete evidence is
`fps8-cvgpoint-{fast,strict,reference}-target.log` under ignored validation output.
Older captures predate the complete coverage-point fixture and cannot satisfy its completeness
requirement. See `STADIUM2_8FPS_PLAN.md` for game-frame error counts
and measured rates; neither focused probes nor renderer speed alone qualifies
an updated player. `FRAME_AUDIT=1 STATE_AUDIT=1` serializes visible RGB565 via
RLE after timing; `check_frame_audit.py` checks complete exports and creates
baseline/candidate/difference PNGs. It reports differences without declaring
a tolerance PASS. Resize the isolated serial console to220x30000 before
starting the pixel export, and220x2400 for state-only captures.

`IDENTITY_SPAN=1` enables an optional software identity rectangle span; check
with `--identity-span`. All builds now include320 independent scalar comparisons
of the complete framebuffer and13 combiner/blender temporary colors. These
cover fixed alpha thresholds, rejected final samples, fractional filters and
depth/dither/real-blend/flipped fallbacks, nonuniform ENV/PRIM RGB products,
direct alpha and COMBINED feedback controls. `COMBINE_SPAN=1` requires
`IDENTITY_SPAN=1` and tests exact simple-combiner specialization; the checker
requires its build identity with `--combine-span`. All builds also require4,096
raw fractional/signed-zero combiner value and temporary-state checks; enabled
builds must exercise all4,096 specialized calls. The final zero additions retain
signed-zero semantics. ARM32 current evidence is `fps8-combinespan2-arm-target.log`;
three native `fps8-combinespan2-{software,fast,reference}-target.log` suites pass,
including same-SH4 reference comparisons. Older320-case `fps8-combinespan-*` and earlier
span/coverage captures retain historical
evidence but predate this additional completeness requirement.

## Shaded packed-color precision (2026-09-30)

`SHADE_SCALE=1` requires `TEXEL_SCALE=1 AFFINE=1`; the main build uses
`RASTER_SHADE_SCALE=1` with `RASTER_TEXEL_SCALE=1 RASTER_SHADE=1`.
This removes packed-texel expansion from the multiplicative vertex RGB in
the four separable shade equations. It preserves the additive bias and alpha,
uses the existing textures/batches, and leaves fractional ARGB4444 unchanged.
It remains opt-in. Pass `--shade-scale` to the replay checker.

Every replay now requires `shade-scale-enabled` and `shaded-precision`:
128 nonuniform color-ramp cases, cold/hot draws, all four shade equations and
RGB565/ARGB1555 texture storage. Equal vertex RGB isolates packed-color error;
unused varying alpha exercises the shade equations without moving RGB samples.
Cold and hot source-alpha draws have separate scalar reference pixels. The
enabled probe allows at most one five-bit step and requires 256 actual GPU
scenes when cutout is enabled. Existing exact checks remain exact; the general
varying/perspective triangle probe keeps its existing two-step bound.

`accuracy-replay-{base,scale,strict,reference}-target.log` records four passing
native suites. The color-ramp channel-error sum falls from1104 to460 (58.3%);
the existing triangle probe falls from75 to55. Strict and the independent SH4
software reference remain exact. Four checker controls reject disabled
correction, truncated completion, absent precision probes and repeated build
identity. These probe results do not establish game-frame equality.

## Triangle attribute origins (2026-09-30)

`TRI_ORIGIN=1` requires `TEXEL_SCALE=1`; the main build uses
`RASTER_TRI_ORIGIN=1`. The correction shifts the U/W, V/W and reciprocal-W
planes together, plus supported RGB shade planes. Vertex x/y and shade alpha
stay unchanged. Rectangles retain their qualified sampling path. Degenerate,
nonfinite, nonpositive reciprocal-W and unrepresentable shade corrections
retain the previous GPU attributes. This capability remains opt-in.

All current replays require a unique `tri-origin-enabled` identity and the
complete `triangle-origin` fixture: 40 independent barycentric projection and
rejection checks, four whole-framebuffer coverage comparisons and four exact
interior texture-ramp comparisons. Enabled fast builds must report seven
corrected triangles and 12 completed GPU scenes. Pass `--tri-origin` alongside
the existing fast-build flags. Software-only builds emit no GPU-scene counters.
Older logs predate this fixture and cannot qualify the current checker.

`LTO=1` in the replay Makefile matches the optimized game configuration.
`accuracy-origin2-lto-replay-target.log` and
`accuracy-origin-{base,strict,reference}-replay-target.log` hold the current
optimized suites; `accuracy-origin2-replay-target.log` also checks the corrected
non-LTO build. The general varying/perspective shade error sum improves from
55 to34 and its maximum from two to one five-bit steps. Six checker controls
reject disabled correction, truncation, absent probes, missing GPU corrections,
repeated identity and a failing footprint comparison. Game-image and sustained
speed qualification are recorded in `STADIUM2_GRAPHICS_ACCURACY.md`.

## Filtered triangle grids (2026-09-30)

`FILTER_GRID=1` requires `TRI_ORIGIN=1 SHADE_SCALE=1`; the main build uses
`RASTER_FILTER_GRID=1` with the corresponding `RASTER_` options. Small textures
(native padded dimensions at most 64x64) in the three supported opaque-source
shade equations store software three-point samples on a quarter-texel grid.
PVR bilinear interpolation then approximates a smaller portion of the filter's
diagonal. The grid keeps logical UV dimensions and uses its physical texel-center
offset. Geometry and shade alpha stay unchanged. Rectangles, point/average
filters, larger textures and binary cutout shade keep the native texture grid.
No unsupported operation is promoted to GPU rendering.

The 12-case `triangle-filter` fixture uses eight high-contrast cold/hot draws,
three shade equations, fractional coordinates and affine/perspective UVs.
Each compares the same-SH4 software image, native GPU grid and corrected GPU
grid. Four controls compare both complete cold and hot framebuffers with the
capability toggled: point sampling, average filtering, a large texture and a
rectangle. Enabled builds require eight actual grid bakes, hot cache reuse,
48 completed GPU scenes, at least a 50% reduction in accumulated channel error
and at most five five-bit steps at any interior probe. Existing exact checks
and their tolerances are unchanged.

The complete LTO corrected/baseline/strict/software suites are
`accuracy-{grid7,grid7-base,grid7-strict,grid7-reference}-replay-target.log`.
Measured error decreases 7453->754 (89.88%), and maximum 16->4 steps. The
checker now requires a unique `filter-grid-enabled` identity, the complete
fixture and all 12 numbered cases; `--filter-grid` enforces the enabled
qualified fast configuration and GPU work. Nine negative controls reject
disabled correction, truncation, missing fixture/case, repeated identity,
missing grid bakes, excessive peak error, lost improvement and failed guards.
Older logs lack this fixture and cannot qualify the current checker.

The earlier half-texel grid reduces accumulated error but reaches nine steps
and fails the original five-step bound. Its game capture also regresses.
`accuracy-grid1` through `accuracy-grid4` preserve those diagnostics; the first
new fixture used incorrect selector constants for two shader groups, corrected
before qualification. The quarter-texel option remains opt-in, and native probe
improvement does not imply comparable whole-game image improvement.
