# Learning from the strict and fast builds

Audit baseline: `50c839b`, 2026-09-21. This is a source-and-evidence audit and
implementation plan. No new benchmark or renderer change was made for this audit.

## Decision

Use strict mode to define regression requirements and the fast build to expose
where work can be avoided or moved to PVR. Develop a third, qualified hybrid by
promoting individual capabilities after paired correctness and cost measurements.
Do not merge the modes wholesale or spend the next cycle optimizing only the
software renderer.

“Accurate” currently means the strict implementation matches tested software
behavior. It does not mean complete N64 accuracy. Analytical fixtures and,
eventually, independent N64 reference results must arbitrate shared bugs.

## What each build teaches us

| Evidence | Strict build | Fast experimental build | What to learn |
| --- | --- | --- | --- |
| Historical menu throughput | 0.586 FPS | 5.451 FPS | PVR offload and reuse can change the cost substantially; isolated CPU improvements are insufficient |
| Draw execution | Tested opaque fills on PVR; textures mostly software | Cached baked textures, hardware triangles and batching | Recover useful capabilities with explicit eligibility rather than a global accuracy switch |
| Untouched framebuffer words | Write mask preserves them for qualified fills | Full readback normalizes alpha and can alter RGB | Transfer write ownership and exact conversion rules into each promoted capability |
| Sampling and combining | Original sampling/filter/combiner order retained | Combines texels before PVR filtering; substitutes bilinear for nonzero filters | Reuse only operations whose order and filter semantics have been proved equivalent |
| State transitions | Same-target pixel/state comparisons | Cold and hot GPU paths fail a following COMBINED consumer | Cache reuse must preserve required state as well as visible pixels |
| Caching | Decoded samples with mutation invalidation | Baked texture reuse, bounded VRAM, ordered eviction | Share invalidation/lifetime lessons; do not assume decoded and baked cache keys mean the same thing |
| Limits | Slow; software coverage itself is incomplete | Approximate alpha/filtering; narrow depth/scissor/shade support | Neither build alone establishes gameplay compatibility |

The fast build is a working throughput experiment, not a correctness oracle.
The strict build is a useful regression oracle, not a finished architecture.

### Rechecked evidence

- `fps5-installed-target.log`: frames 60–480, 420 frames / 77.050234 s.
- `floor-game.log`: frames 60–120, 60 frames / 102.413547 s.
- `quality-replay-fast.log`: partial-fill RGB/alpha corruption, 502 ramp
  mismatches, mixed-path corruption, and COMBINED consumers returning `0001`
  after a red `f801` producer. These are actual failed synthetic expectations,
  not just a failure caused by missing newer fixture names.
- `floor-kos.log` versus `floor-kos-reference.log`: strict replay passes.
- `triangle-profile/menu.log`: host textured triangles account for about 86%
  of raster time. This identifies candidates; it is not a target cost breakdown.

All logs above are local under ignored `build/dc/validation/`.

The fast measurement predates the latest software optimizations. The two numbers
are not a controlled same-revision comparison. New decoded-cache/floor changes
can also affect texture baking and fallback in fast mode; their benefit there
has not been measured.

## The performance budget changes the priority

| Historical target interval | Total ms/frame | Raster/list ms/frame | Everything outside that region |
| --- | ---: | ---: | ---: |
| Fast | 183.45 | 70.63 | 112.82 |
| Strict | 1706.89 | 1597.26 | 109.64 |

At fixed strict non-raster cost, 5 FPS leaves about **90.36 ms** for rendering.
That would require about **17.7x raster throughput**, or an **8.53x overall
speedup**. This is conditional budget arithmetic, not a measured achievable gain.
The fast build has only **16.55 ms/frame** of historical headroom below 200 ms.

Consequences:

1. Exact CPU improvements remain useful, particularly for fallback, but another
   small cache improvement is not a sufficient route to 5 FPS.
2. Price correctness repairs on the fast architecture, as well as measuring
   how much strict work a capability replaces.
3. Measure clusters of adjacent draws. An individually fast GPU draw can make
   a frame slower if it introduces extra drains, imports or cache eviction.
4. Keep nested timers separate: GPU wait/readback are already inside raster/list
   time. The remaining time is not automatically all interpreter work.

## Audit gaps to close before drawing stronger conclusions

**The strict switch changes several things at once.** In
`platform/dc_raster_pvr.cpp`, it disables triangles, restricts fills, changes
readback ownership and disables additional framebuffer dithering. Toggling it
cannot isolate the price of alpha preservation, filtering, state or batching.

**Current texture tests do not establish GPU texture correctness.** Strict
triangles immediately fall back. The combined-state producer can therefore pass
in software, and the 64 sampler cases call TX directly. Existing GPU scene
requirements cover fills. Add actual textured-triangle fixtures and per-capability
GPU execution counts before promoting textured operations.

**`REFERENCE=1` is not a pure-software KOS renderer switch.** The replay Makefile
still enables PVR on KOS; this flag disables recent software optimizations.
Keep that comparator, but add a genuine software-only SH4 replay configuration
for GPU differential tests. ARM and SH4 raw-float comparisons cannot substitute
for same-target comparisons near three-point filter boundaries.

**The existing aggregate hashes locate a case, not the first bad pixel or draw.**
Preserve them as quick checks, then emit detailed mismatch information only for
failing cases. Current fallback counters also omit some rejection paths and do
not measure rejected pixels or transition costs.

## Ordered experiment plan

### 1. Establish a controlled comparison

Build strict and experimental modes from the same source revision, compiler,
KOS, options and scene inputs. Use separate build directories, executables,
CDIs and emulator processes. Retain the archived 5.45 FPS image unchanged.

Record a manifest containing revision/dirty state, compiler and KOS versions,
flags, ELF/CDI hashes, emulator/backend settings, private ROM/checkpoint hashes,
input schedule, warmup and measurement limits. Keep private game data local.

Use four roles:

| Role | Purpose |
| --- | --- |
| Same-target software replay | Pixel/state comparator; no PVR draws |
| Current strict mode | Known passing hybrid baseline and fallback performance |
| Current experimental mode | Throughput baseline and expected-failure control |
| One-capability candidate | Strict baseline plus exactly one qualified GPU feature, or fast mode with one isolated repair |

First deliverable: a matched baseline report for menu, opening, nearest/clamped
rectangles, and synthetic correctness cases. Do not claim a new FPS result from
the historical comparison in this document.

### 2. Find the first divergence and account for the work

Add bounded replay records at the RDP draw boundary: command index, primitive,
cycle, sampler/combiner/blender state, scissor/target, relevant texture data and
dependency reads. Replay identical command prefixes in isolated processes.
Reconstruct renderer state from values and commands; do not shallow-copy live
C++ objects containing pointers. PVR caches are currently global, so two RDP
instances are not independent shadow renderers.

On failure, report the first divergent draw, pixel coordinate, expected/actual
RGBA5551 word, untouched/covered classification, depth and required register
differences. Include cold/hot cache, eviction, reset and mixed GPU/software runs.
Use full comparisons for failures, not just a screenshot or an aggregate score.

Collect counts, covered/rejected pixels, fallback reasons, cache reuse, flush
reasons, import/readback bytes and time by capability. Include framebuffer reads
used by later texture or RSP work when testing ordering. Use instrumented runs
for attribution, then disable expensive diagnostics for throughput measurement.

Exit: every observed operation is accounted for; the known fast failures are
reproduced and localized; strict does not pass a GPU test by silently falling back.

### 3. Promote the smallest valuable GPU subset

| Candidate, in proof order | Main lesson borrowed | Gate before promotion |
| --- | --- | --- |
| Exact opaque fill batching | Fast batching plus strict write ownership | Existing ramps, disjoint writes, batch boundaries and CPU edits still pass; measure mask/readback cost |
| Opaque, integer-aligned nearest texture rectangle with a simple mux | Fast texture residency plus strict sampler/state behavior | Exact channel/alpha ramps, cache cold/hot, tile mutation, scissor and following software draw; require actual GPU work |
| Opaque nearest textured triangles | Fast triangle offload targets the profiled workload | Edge/coverage, shared edges, perspective, coordinate tie cases and final state match; no bounding-box write-mask substitute |
| Binary-alpha cutouts | Fast pixel-aligned specialization plus strict rejected-pixel preservation | Holes retain old RGB/alpha/depth; threshold and overlap boundaries pass |
| Fractional filtering, nonlinear/two-cycle mux, general alpha and depth | Both builds expose the hard cases | Separate analytical/independent-reference proof for each; remain fallback until qualified |

The rectangle is a proof vehicle, not the expected large menu speedup. Promote
triangles only if the new trace shows the eligible subset covers meaningful cost.
Do not assume nearest/simple-mux triangles dominate the current menu.

Color transport is an early gate: bin-origin quantization proved exact for
constant fills, but RGB565 texture sampling expands channels and can reintroduce
readback rounding. Test any supported palette/texture representation on target
before selecting it. Do not assume a 32-bit direct PVR texture format exists.

Likewise, fixing final combiner state cannot mean applying the last baked texel.
Determine the required state from the actual covered/passing samples and the
defined command behavior, and ensure hits and misses agree. Resolve disagreement
with N64 semantics explicitly rather than preserving every legacy temporary.

### 4. Transfer performance mechanisms only after equivalence

Share decoded-data invalidation and bounded resource lifetime across modes;
keep baked combiner products conditional on their proven semantics. Reuse and
batch consecutive compatible draws without reordering them. Compare one repair
at a time, then combine passing repairs and remeasure their interactions.

Keep the nearest/clamped regression as a mandatory performance case. Test cache
admission or canonicalization against wrap, mirror, clamp, shifts, palette changes,
invalid samples and generation wrap. A faster menu is not grounds to delete the
regressed case.

For every experiment, keep a small results ledger: input/build identity, required
GPU count, first correctness failure or pass, target milliseconds saved, added
transition cost, RAM/VRAM cost, and retain/reject decision. Prioritize measured
net frame-time savings among passing candidates, not isolated triangle rates.

### 5. Qualify the combined build

Advance from menu/opening checkpoints to normal boot, file creation, indoor and
outdoor play, depth overlap, transparency, pause, scene transitions and save/load.
Measure emulated-time progress and VI/list/present counts separately. Add audio
produced/consumed/silence counters and listen to a captured run.

Retain the existing release gate: three runs of at least 600 post-warmup frames
per claimed scene, each averaging at least 5 FPS, with frame-time distributions,
zero decode/presentation failures and passing correctness fixtures. Report the
supported emulator configuration; physical Dreamcast claims require hardware
validation. The Downloads launcher changes only after the candidate earns its
stated quality/performance status.

## Recommended next work session

Implement steps 1 and 2 first: matched builds, genuine software-only SH4 replay,
and first-divergence/capability accounting. The deliverable should be a ranked
list of actual menu draw groups with correctness failures and measured costs.
Then choose the first texture capability from that evidence. Keep the small
cache-regression repair as bounded supporting work, rather than the main route
to the 5 FPS goal.

Related: [progress checkpoint](../../PROGRESS.md),
[existing correctness audit](FPS_QUALITY_PLAN.md),
[replay instructions](RASTER_REPLAY.md), and [experimental backend](PVR_RASTER.md).
