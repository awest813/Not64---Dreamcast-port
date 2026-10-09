# Pokémon Stadium 2: emulator audit and 8 FPS work plan

Audit: 2026-09-30, current working tree, GCC 15.1/KOS 2.3.0.
This supersedes earlier performance targets for this task. The previous
[performance report](STADIUM2_PERFORMANCE.md), [quality audit](FPS_QUALITY_PLAN.md)
and [capability plan](DUAL_BUILD_LEARNING_PLAN.md) remain evidence and history.

## Target and acceptance

The requested metric inherits the user's earlier clarification: **8 presented
game-progress VI updates/s**, with fresh drawing throughput reported separately.
This is a practical progress proxy, not an independently measured simulation
tick rate. Flycast's host refresh rate cannot qualify it.

Keep steady mode 7: skip 2,3,3,3, four drawn lists per fifteen, average 2.75
skips and maximum three. Do not increase skipping to reach the number. At this
game's observed one-list-per-two-VIs rate, exactly 8 progress/s would produce
about 1.07 fresh drawing presentations/s under this policy.

First qualification: battle commands and the native 640-wide interlaced rental
roster, each from the existing private checkpoint and with the same controls,
compiler, settings and policy. Warm up through VI 60, measure through VI 600,
and require **both average and every 60-update window >=8**. Repeat qualifying
runs once, then qualify the actual unlimited live-control player. Add move
selection, the held-R panel and an actual attack/return-to-menu sequence before
describing performance as overall gameplay. Existing action360 checks establish
menu progress, not a completed attack or battle.

Use `measure_game_perf.py --metric vi --start-frame 60 --end-frame 600
--window-frames 60 --min-fps 8 --min-window-fps 8`. Preserve raw logs, build
flags, toolchain/backend identity, checkpoint/input hashes and ELF/CDI hashes
under ignored `build/dc/validation/`. Detailed profiling is diagnostic only;
qualification builds have CPU/SOFT/RASTER profiling disabled. Keep the passing
3.8 player until a replacement passes. No game data belongs in the repository.

## Latest measured candidate status

The best isolated renderer candidate remains below the target:

| Candidate | Battle progress/s / min window | Rental progress/s / min window | Qualification |
|---|---:|---:|---|
| Larger pinned texture batches, identity/cutout | 4.61276 / 4.33194 | 6.03591 / 5.93901 | VI60-600, unchanged frameskip; experimental fast renderer |
| Above + opaque-alpha proof + clipped integer rectangles | Pending | 6.14947 / 6.04733 | VI60-600; focused SH4 GPU probes pass; player unchanged |
| Corrected rectangle sampling/packed colors + indexed cache + deferred PC publication | 4.67674 / 4.38969 | 6.21161 / 6.11562 | Isolated VI60-600, all detailed profiling disabled; still below8 |
| Above + inline pure-core Count updates | 4.82806 / 4.52864 | 6.37671 / 6.27985 | Isolated VI60-600; matching states/native boundary tests pass; still below8 |
| Above + exact software identity rectangle spans | 4.82849 / 4.52864 | 6.77731 / 6.66448 | Isolated VI60-600; scalar/native/state comparisons pass; still below8 |

The corrected fast renderer now passes the complete current SH4 replay suite,
including cold/hot COMBINED feedback, packed texture precision, initial spans
and deliberate full-hash collisions. Strict replay also passes its same-SH4
software reference. These are opt-in capabilities; game-frame equality is not
established. The rental VI120 export still differs from software at126385 pixels,
mean RGB byte error2.07547, maximum8 five-bit channel steps (eight pixels).

Expanded dispatch, local-PC, handwritten threaded and native-block CPU variants
pass matched VI120 state comparisons but are slower. Deferred PC publication
passes both states and ARM32 regressions; short diagnostic other cost improves
154.043->151.728ms battle and82.212->80.800ms rental. Long isolated measurement
of the combined corrected renderer/index/PC candidate confirms the small gain.
Inline Count updates improve the combined candidate further: battle
raster70.502/other128.423ms, rental raster59.665/other79.426ms. Remaining savings
to the125ms/VI budget were82.123ms battle and31.839ms rental. The retained
software span improves long rental raster to50.354ms with other79.482ms;
latest residual gaps are82.104ms battle and22.551ms rental. No8 FPS
player is qualified. Historical four-failure replay results below refer to older
renderer builds, before the packed-color and post-combiner corrections.

## Baseline and required improvement

Matched final steady measurements from `fps38-steady-*-measure.json`:

| Scene | Progress/s | Minimum window | Fresh drawings/s | Raster ms/VI | Convert | Present | Other | Total ms/VI |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Battle, unlimited live player | 4.28497 | 3.82713 | 0.57133 | 88.493 | 8.141 | 0.060 | 136.680 | 233.374 |
| Rental roster | 3.95994 | 3.91964 | 0.52799 | 149.742 | 17.657 | 0.059 | 85.072 | 252.529 |

8 progress/s allows 125 ms/VI. Battle needs 108.374 ms saved (46.4%); rental
needs 127.529 ms saved (50.5%). These are necessary average reductions; the
slowest windows also must improve. Raster time includes RSP display-list
decode, GPU preparation, software drawing and GPU waits/transfers. RSP type-1
time contains that region and must not be added again. Other contains CPU,
RSP audio, event handling, input, scheduling and logging, not just CPU.

Even zero battle raster work leaves 144.881 ms/VI, a ceiling of about 6.90
progress/s. At unchanged rental non-raster cost, only 22.213 ms remains for
raster work, versus 149.742 today. The plan must improve CPU execution and
renderer work together. Presentation is already negligible in Flycast.

One allocation to aim at, **not a forecast**: battle other <=70, raster <=45,
conversion <=9 ms; rental other <=60, raster <=47, conversion <=17.7 ms.
Both fit under 125 ms with current presentation. Reallocate after profiling.

The corrected candidate with inline Count now needs another82.123ms/VI saved
in battle and31.839ms/VI in rental. Its battle non-raster cost is136.620ms, so
eliminating all remaining raster work still cannot reach8. Rental could
theoretically fit with raster<=27.826ms at unchanged other cost, versus59.665ms
measured. The CPU
and raster milestones remain necessary. Evidence is
`fps8-qual1-{battle,rental}-{target.log,measure.json}`; the abandoned initial
VI60-only battle capture is explicitly retained as an incomplete log and excluded.

## Audit by subsystem

| Subsystem and source | Current behavior/evidence | Work and priority |
|---|---|---|
| CPU: `r4300/pure_interp.c`, `platform/dc_interp_decode.h` | Pure interpreter; common ALU/load/store labels and fresh direct RAM fetch already avoid some calls. Generic primary/SPECIAL/COP1 and most delay instructions still use handlers. Prior branch inlining regressed, so it was removed. | P0: inspect generated SH4 code and measured instruction mix; reduce dispatch/register spills with a bounded block executor. P1: SH4 native blocks if interpreter savings cannot meet the budget. |
| CPU timing/exceptions: `r4300/r4300.c`, `interupt.c`, `exception.c` | Count advances at existing branch/device boundaries; self-branch/NOP idle recognition exists. No evidence currently proves a larger safe idle loop. | Preserve delay slots, branch-likely annulment, interrupt order, Count/Compare wrap, EPC/BD and instruction limits. Do not infer idle behavior from approximate PC weights or change `count_per_op` for speed. |
| CPU bus/TLB: `platform/dc_interp_memory.h`, `gc_memory/memory.c`, `TLB-Cache-hash.c` | RAM shortcut checks installed width-specific handlers; framebuffer/MMIO/TLB callbacks retain original dispatch. Hash TLB indexing and free-node reuse already fixed. | P1: quantify ordinary RAM versus device/translated traffic and hoist only proven repeated qualification. Cache invalidation must cover mappings, hooks, CPU stores and DMA. |
| RSP graphics: `mupen64_soft_gfx/rsp.cpp`, `platform/dc_soft.cpp` | New RSP object/command table and microcode scan per list; matrices/clip-space vertices still execute on skipped lists because CULLDL changes control flow. Skipped lists omit draw-only attributes/projection. Persistent RDP state and FullSync remain. | P1: profile decode, matrix/vertex work, skipped versus drawn lists. Reuse immutable command maps/microcode identification only with mutation checks; lazy clip-space transforms must preserve CULLDL and invalid-index errors. |
| RDP software: `rs.cpp`, `cc.cpp`, `bl.cpp`, `tx.cpp`, `tf.cpp` | Float per-pixel filter/combiner/blender/depth fallback. Decoded samples, unused TEXEL1 omission, exact identity blending, floor shortcut and opaque rectangles are already implemented. Fill-span experiment gave no gain and was removed. | P0: rank fallback states by time and covered pixels. Specialize a frequent exact shader/span, preserving quantization, sampling order, rejection and final temporary state. Do not repeat unmeasured fill work. |
| PVR texture cache: `platform/dc_raster_pvr.cpp` | Default128 entries/2 MiB VRAM; opt-in batch cache has256 entries and retains completed-batch textures. Mutation-aware adjacent hits avoid redundant4.5 KiB comparisons; optional512-bucket index caches the content-hash prefix between TX mutations. Full keys remain authoritative, including deliberate hash collisions. | P1: long-run indexed-cache cost; distinguish descriptor-only mutation from TMEM/palette mutation before introducing finer epochs. Keep shader dependency masks, pinned eviction barriers and bounded residency. |
| PVR eligibility/batches: same | Depth is rejected, varying shade supported only for three opt-in equations, strict textures remain software. Many target submissions and software transitions. Regional native-width import/readback already implemented. | P0: record flush reason/area and rejection reason/state. Extend only observed safe capabilities; preserve barriers before framebuffer consumers and software writes. Optimize ordering without moving guest draws. |
| PVR correctness | Strict and corrected full fast same-SH4 replays pass. Packed texture scale and rectangle post-combiner restoration fix the four historical groups. Game-frame export improves but still has approximate triangle/filter/edge differences; regional readback can normalize untouched pixels inside its bounding box. | P1 alongside speed: investigate the eight largest rental pixel errors; add triangle producer/COMBINED consumer fixtures and framebuffer ownership probes. Keep exact checks unchanged. Synthetic replay alone does not qualify game fidelity. |
| VI: `platform/dc_vi.c`, `dc_gfx.c` | Exact paired RGBA16 conversion, native aligned loads, packed half-row averaging, padding cache and two field snapshots. Rental conversion dropped from about 180 to 17.7 ms/VI. | P2: optimize remaining measured fractional/unaligned modes only. Preserve field refresh and invalidation on layout/lifecycle/CFB writes. Native resolution must remain; stale fields cannot buy the target. Full N64 divot/coverage/dither VI fidelity is separate accuracy work. |
| Presentation: `platform/dc_pvr.c` | Ping-pong scanout and DMA; ~0.06 ms/VI here. Upload hashing previously cost more than saved DMA and remains off. | P3 in Flycast; profile real hardware before further upload/synchronization work. RTT readback waits are in raster, not present. |
| Audio HLE: `rsp_hle/` | Stadium dispatch includes NEAD MM and other task types; old battle profile measured ~11.3 ms/VI audio. Resample/envmix/ADPCM loops are candidates, not yet separately priced. | P1 if updated cost is material: per-command timing/counts, then exact integer kernels preserving saturation, ramps/history and task completion. PCM/state differential fixtures before SH4 tuning. Muting output alone does not eliminate HLE work. |
| AICA: `gc_audio/audio-dc.c` | Worker polls every 5 ms; ring locks, word-swapped PCM conversion and silent underrun padding. Stream initialization proves startup, not sustained sound. | P2: report underrun bytes, worker CPU and lock wait; bulk conversion if measured. Never suppress audio RSP/AI completion to inflate throughput. Slow emulation still starves real-time audio at 8 progress/s. |
| ROM/DMA: `main/ROM-Cache-dc.c`, `gc_memory/dma.c` | 1 MiB/64 KiB blocks with O(1) hit timestamp; 64 MiB private ROM streams from disc. PI invalidation sweep fixed; direct ROM execution fallback preserved. | P1 instrumentation: page-ins and I/O time per window/scene transition. Tune cache/prefetch only if misses dominate, within RAM budget; loading time is separate from steady FPS. |
| RAM/VRAM: `platform/dc_memory.h` | Wide raster scratch2.25 MiB, RDRAM4, ROM1, VI1, soft544 KiB, audio80 KiB, TLB/misc512 KiB, OS/code reserve3. Paper remainder3,817,472 bytes. Presenter2 MiB +max RTT2 MiB +texture cache2 MiB consume most VRAM before allocator/TA/framebuffer overhead. | P0 before block/JIT cache: measure actual heap high-water and VRAM free under both scenes and transitions. Bound caches, evict safely, provide allocation fallback. The paper reserve is not runtime free heap. Expansion Pak is outside current coverage. |
| Input/PIF/saves: `gc_input/`, `gc_memory/pif.c`, `platform/dc_savestates.c`, `dc_nativesaves.c` | Live Maple path/replay share mapping. No measured input overhead problem. Checkpoint uses streamed explicit-width format/CRC rather than a whole-file allocation. Saves and input are tested but gameplay coverage remains narrow. | P2 functional: normal boot, controller responsiveness, scene transitions, save/reload and ROM reopen. Timing changes must preserve PIF/device effects; keep synthetic benchmark input separate from final player. |
| Build/test/reproducibility: `Makefile.dc`, `tests/dc/`, `tools/dc/` | ILP32 contract mandatory; ARM32/QEMU regression, same-target software/strict/fast replay, timing parser with negative controls. Dirty tree contains prior authorized work. Flycast 2.6/OpenGL pinned; physical Dreamcast unverified. | P0: isolated flag-stamped build dirs/artifacts, one measured Flycast process, manifests and complete logs. CPU kernels need explicit self-modifying/DMA/delay/MMIO tests beyond the current short CPU ROM. |

## Ordered implementation with decision gates

### What the parity checks establish

State comparisons establish preservation of this port's existing behavior.
They do not establish a fully accurate R4300/RDP implementation. In particular,
`r4300/macros.h` makes FP rounding setters no-ops on this non-x86 build; several
integer overflow/address/TLB exception functions stop emulation rather than
deliver an architectural exception; the software blender has no subpixel
coverage buffer and simplified encoded-depth comparison. Host FP conversions,
NaNs, signed arithmetic and unaligned access need separate accuracy fixtures.
The native block oracle preserves the legacy register-zero behavior, which is
not proof of architectural register-zero fidelity. These are accuracy work,
not permissible shortcuts to the speed target.

Release confidence also remains limited by checkpoint-based coverage: normal
boot/ROM reopen, actual attacks, save round-trips, sustained audio and physical
Dreamcast timing have not qualified this8 FPS candidate. A checksum match does
not replace complete gameplay coverage or an image-error assessment.

### A0 — complete current profiling

Run the current steady core at VI 120 with CPU/SOFT/RASTER profiling in both
checkpoints. Use full 600 only where animation phases change the ranking. Report
exact opcode counts, RSP audio, draw categories and state costs; approximate PC
Space-Saving weights include replacement error and are hints only. Capture
counter snapshots at VI60 and 120 so warm differences are explicit. Improve
profiling where needed: bounded CPU hot-block counts, draw/skip decode times,
texture hit/miss cost, fallback covered area, flush reasons, ROM I/O and audio
underruns. Time nested components without double-counting them.

Exit: a quantified next experiment for each scene and a reproducible baseline.
Diagnostic overhead or serial printing cannot count as a performance gain.

### A1 — cheap exact cache and dispatch experiments

1. Avoid rescanning TMEM/palette on an adjacent texture cache hit if the same
   live TX object has had no mutation. Track mutation explicitly, invalidate on
   all texture/tile/LUT writes and renderer reset, and test epoch wrap/object
   replacement. Compare small shader inputs every call; retain content checks
   after mutations, so identical reuploads still hit. Measure unprofiled paired
   battle/rental runs. Remove the shortcut if cost or regression outweighs it.
2. Inspect SH4 assembly for generic op dispatch, 64-bit GPR spills, bus-global
   traffic and FPSCR changes. Test one narrow operation group at a time. The
   already-regressing branch-inline/O3 attempts are controls, not proposals to
   reintroduce without a distinct mechanism.
3. Instrument RSP initialization and immutable microcode/command-map reuse.
   Mutation-aware identity must precede reuse; graphics task state still resets.

Exit: exact regressions/state comparisons and repeatable target cost reduction;
do not broaden optimizations that merely compile or pass a microbenchmark.

### A2 — CPU block execution, escalating to SH4 native code

Build a bounded optional executor for short straight-line integer blocks in
ordinary first 4 MiB KSEG0/KSEG1 RAM. First reduce repeated dispatch/fetch setup
without changing existing branch, interrupt, FPU, TLB or device handlers.
Blocks end before any unsupported operation or boundary requiring precise state.
Read current instruction words or validate the entire executed prefix before
reuse; fresh stores/DMA/mapping changes must never execute stale instructions.
Do not assume a page checksum alone prevents collisions or mid-block writes.
Use a small explicit RAM allowance and fall back on allocation/execution gaps.

Differential cases: high GPR halves/sign extension; register zero behavior;
overlapping source/destination; signed/unsigned arithmetic; forward/backward
and likely branches; branch/delay exceptions; RAM hook/MMIO fallback; code
patching via KSEG aliases/CPU stores/PI/SP DMA; Count wrap/interrupts; checkpoint
load/ROM close; bounded diagnostic stops. Compare CPU/device/memory state at
block exits and at identical VI checkpoints. A native JIT also needs SH4 code
cache coherence, safe executable arena/literal reach, ABI register/FPSCR rules
and invalidation under every writer; the PPC recompiler cannot simply be linked.

Gate: if dispatch-only work cannot plausibly reduce battle other toward 70 ms,
implement SH4 integer native blocks before adding another small interpreter
shortcut. Start with ALU and guarded RAM loads/stores, then branches and FPU
from observed coverage. Unsupported instructions exit to the original core.
This is the largest engineering item and a performance hypothesis until measured.

The initial native implementation has now failed its performance gate despite
passing state tests. Do not continue adding opcode coverage to that design
without reducing its measured overhead. The current store/reuse/frontier
version still averages only about4.7 native instructions per hit, calls through
the C ABI per prefix, revalidates every word, repeatedly qualifies RAM handlers,
and writes guest registers after each operation. Its larger fixed code slots
also reduce cache capacity. Follow this sequence:

1. Disassemble the hot hit/validation path and generated RAM sequences; separate
   lookup, code validation, memory guards and native execution costs in an
   untimed diagnostic. Record prefix length distribution and useful compiled
   bytes versus fixed-slot waste. Do not optimize from hit rate alone.
2. Test sharing a word-RAM handler proof within one observer-free prefix.
   Require both installed read/write handlers for that page, fresh code and
   unchanged mapping; retain width/alignment/zero-address and store-overlap
   checks. Restore any saved SH4 register on every side exit. Test same/different
   pages, replaced width handlers, aliases and mid-prefix self-modification.
3. If per-prefix overhead still outweighs dispatch saved, replace the fixed
   layout with a bounded code arena and direct links between known block exits.
   Keep full code-word validation initially. A later page-generation scheme
   requires a documented inventory of every CPU, PI/SP/SI DMA, RSP, software
   framebuffer, GPU readback and checkpoint writer. Aliases share physical
   generations; generation wrap and checkpoint/reset invalidate all links.
   Never drop freshness checks before that inventory and tests are complete.
4. Add register residency and branch/delay execution only after linked execution
   wins on target. Flush every required guest/bus/PC value before original
   handlers, interrupts, faults and diagnostic exits. Test likely annulment,
   delay exceptions, register31 aliases, Count/Compare wrap and code patches
   arriving through every writer. Add FPU bit transfers first; arithmetic needs
   a declared compatibility contract for the port's existing FP behavior.

Advance each step only on matched state evidence and a measured reduction.
The target CPU budget is roughly half current battle other cost; a two-millisecond
interpreter saving is useful but does not justify claiming the architecture gap
is solved. Keep slower experimental kernels disabled.

### A3 — lower drawn-frame raster cost

Price the top two fallback states in each scene. For a common exact software
shader, hoist stable checks/inputs to a span and keep original arithmetic order,
depth/alpha rejection and final state. For a PVR candidate, add same-SH4 fixtures
with actual GPU execution before broadening eligibility. Extend batch transfer
ownership only when untouched words and barriers are proven. Texture mutation
versions can also amortize key hashing/snapshots beyond adjacent hits; keep
exact bytes for collision resolution and bound new storage.

Native-width rental priorities depend on texture bake/cache versus fallback
profile; battle likely needs depth/shade work. Do not enable GPU depth globally:
N64 encoded depth, coverage and PVR ordering are different contracts. Do not
introduce reduced-resolution rendering or more skip as an unrequested shortcut.

Exit: lower raster time in both long benchmark scenes, strict replay passing,
fast expected failures unchanged or repaired individually, clear capability limits.

### A4 — audio, memory traffic and remaining costs

Use A0 counters to choose resample/envmix/ADPCM or ROM/DMA work. Apply exact
integer SIMD-like paired/unrolled operations only after checking saturation,
unaligned/odd lengths and saved histories. Bulk PCM conversion under the ring
lock can reduce worker CPU if it is actually measurable. VI work follows only
if conversion remains a significant residual once CPU/raster improve.

Exit: PCM/history and device parity, no worse ring starvation or allocation
failures, measured target improvement. Never count skipped AI/RSP work as speed.

### A5 — steady 8 qualification and player delivery

Combine only retained gains; disable detailed profiling. Run the two long scenes,
check all nine windows, repeat a passing result, then use live unlimited settings.
Exercise move panel, actual attack, return, scene/load transitions and save/reload.
Record fresh draws and policy on every result. Build a separate 8 player/launcher
only after qualification, preserve the previous player, and report any remaining
scene/accuracy/audio limitations. Physical Dreamcast measurement is a separate
support gate; Flycast results cannot establish hardware 8 FPS.

## Status / experiment ledger

- Baseline: both scenes pass the former 3.8 criterion; neither reaches 8.
- A0 complete for the initial 120-VI scenes: detailed profiler artifacts/logs are
  `stadium-8audit.elf` and `fps8-audit-{battle,rental}-target.log`. Battle retires
  15,260,538 instructions in VI 60-120 (including delay slots); warm audio HLE
  is 11.310 ms/VI in battle and 4.210 ms/VI in rental. Approximate PC weights
  do not establish an idle-loop optimization.
- Battle profile: two-cycle textured/depth triangles cost 6.518 s over the
  entire 120 updates. Blender `0c1841c8`, combiner `00127fff/3ffe7238` costs
  3.354 s by itself; it uses varying shade and direct texture alpha. Rental's
  one-cycle rectangles cost 17.441 s; identity blender `0f0a4000` with direct
  texture RGB costs 4.398 s and its ENV/PRIM interpolation equation costs
  5.733 s. These nested costs identify candidates, not additive timing totals.
- A1 retained adjacent-TX mutation proof: all 15 texture/tile/LUT/reset/object
  mutation fixtures pass, strict same-SH4 replay matches its genuine software
  reference, ARM32/high-resolution regressions pass. Rental VI 60-600 improves
  from 3.95994 to **4.02879 progress/s**, minimum **3.98478**, fresh **0.53717/s**.
  Raster is 145.439 ms/VI (4.303 ms lower); policy/counts remain unchanged.
  Evidence: `fps8-cache-rental-{target.log,measure.json}`. Battle long-run
  measurement for this isolated change remains pending.
- A3 identity blender candidate (`RASTER_IDENTITY=1` requires `RASTER_SHADE=1`, fast mode only) passes its
  focused genuine SH4 GPU fixture, including fixed-threshold fallback. Strict
  replay passes against software. Full fast replay retains the same four known
  failing groups. Isolated identity-only rental measurement regressed to **2.40999 progress/s**,
  minimum **2.39474**, raster **312.169 ms/VI**, fresh **0.32133/s**. It is
  rejected for player use unless a subsequent measured fix removes the regression.
  The log shows thousands of eviction-related GPU submissions; baking constants
  that an equation does not use is a newly identified cache-churn cause.
- A2 first optional native integer/RAM executor (`INTERP_BLOCK=1`) passes
  1,024 randomized 64-bit-register cases, 80 RAM cases, eight hook/MMIO exits,
  zero/alignment/boundary/code-mutation/cache-reset cases. Its first zero-address
  encoding bug was fixed before game validation; the failed log is preserved.
  The original direct-mapped cache then passed battle VI 120 state comparison:
  explicit CPU/event bytes, MMIO and region/page/scanout CRCs all match the
  interpreter. These CRC comparisons are checksum evidence, not an exported
  byte-for-byte full state; the derived TLB cache/audio ring are excluded.
- That first native cache is a **rejected performance experiment**: warm VI
  60-120 takes 41.721 s (1.438 progress/s), with 2,735,153 compiles by VI 120.
  The speculative cache admitted useless one-instruction starts and thrashed.
  It must not replace the qualified player. Logs: `fps8-state-block-battle-*`.
- A2 revised cache: 1,024 entries/four-way sets, 471,040 bytes within the same
  512 KiB reserve, skips unsupported and single-instruction starts before cache
  admission. It passes the same synthetic tests plus 136 collision/hot-reuse
  cases. It passes battle and rental VI 120 state checks against the original core.
  Untimed diagnostic runs still show higher CPU cost; do not count that as a gain.
  A separately reserved 2 MiB/four-way 4,096-entry variant (1,884,160 actual
  bytes) passes synthetic tests and is in paired game-state/memory diagnostics.
  `INTERP_BLOCK_KB` accepts 512/1,024/2,048; the smaller reserve is the default.
- A3 texture-alpha varying-shade experiment is separately opt-in
  (`RASTER_CUTOUT=1` requires `RASTER_SHADE=1`). It retains binary source alpha
  and rejects fractional source alpha. Focused cold/hot, filtered, transparent
  and fallback fixtures pass with six genuine GPU scenes, maximum two five-bit
  channel steps at interior probes and no alpha-bit mismatches. A stronger
  primitive-color-change probe exposed a three-step error; the mode was narrowed
  to RGB components >=210, with lower values explicitly tested for fallback.
  The current fixture has five cases; the earlier four-case pass is historical.
  Existing exact
  replay tolerances remain unchanged; bounded interior fast-color probes do not
  establish general renderer accuracy. Current full fast replay still has the
  same four failed groups. Its long-run game rate remains unmeasured.
- Texture-key dependency optimization: separate RGB and alpha masks for each
  baked constant, ignore zero-product/direct unused inputs, and omit constants
  submitted at vertices in raw-texture shade modes. Twelve exact semantic/cache
  reuse cases pass on ARM32 and SH4; genuine strict/software replay also passes.
  The narrowed five-case shade rerun passes on SH4. It retains the four
  known full-fast failures; strict replay passes against the genuine software
  reference. VI 120 rental state with the dependency-aware key matches the
  previous renderer state, including scanout and page CRCs, but short diagnostic
  raster cost remains essentially unchanged. It has not removed the identity
  blender regression.
- Next: qualify larger-cache state and runtime memory in both scenes; measure identity-only and
  combined retained candidates alone through VI 600; retain only actual gains.
  A4/A5 and broader actual attack/return/save coverage remain outstanding.
- The 2 MiB native cache passes both VI 120 game states. Runtime diagnostics
  report about 3.648 MB available main RAM, 1.734 MB live heap, and 2.28-2.42 MB
  free VRAM in these two scenes. This is two sampled scenes, not a transition
  high-water/leak audit. Warm CPU cost still exceeds the original interpreter.
- Next CPU experiment: force eligibility checks inline after SH4 disassembly
  revealed two helper calls and an FPSCR setup on attempted block entry. Native
  code remains opt-in; no native variant is approved for the player yet.
- Next renderer experiment: `RASTER_BATCH_CACHE=1` doubles entry capacity to
  256 with a 3 MiB scratch reservation, keeps the same 2 MiB texture VRAM cap,
  and pins only textures referenced by the pending GPU batch. Evicting a texture
  last used by a completed batch avoids a completion barrier. Evicting a pinned
  texture still completes the batch before freeing its storage. A new 300-texture
  rectangle/triangle fixture tests both paths. Genuine SH4 tests and isolated
  rate measurements are pending. The broad identity blender is now separately
  opt-in and remains off by default after its measured regression.

- Batch retention completed genuine SH4 rectangle/triangle cache probes with
  four scenes, 300 misses each, zero/one pinned eviction barriers. Strict replay
  passes its same-SH4 software reference; fast retains the same four known
  failed groups. Isolated VI60-600 rental improves to **6.03591 progress/s**,
  minimum **5.93901**, fresh **0.80479/s**, raster **62.883 ms/VI**. Battle
  improves to **4.61276**, minimum **4.33194**, fresh **0.61503/s**, raster
  **71.863 ms/VI**. Other costs are unchanged: rental85.066/battle136.730 ms.
  Both remain below 8. Evidence: `fps8-batch-{scene}-{target.log,measure.json}`.
  No frameskip increase; each warm run draws72/skips198 lists.
- Next guarded candidates: exact final-alpha255 proof for one-cycle coverage
  rejection and integer rectangles already clipped to their scissor. General
  triangles, fractional rectangle origins, varying/textured final alpha retain
  the existing fallback. New framebuffer/eligibility probes are in SH4 replay.

- Alpha/scissor cold/fallback probes pass on genuine SH4; strict/software
  reference passes; fast retains four known failed groups. Isolated rental
  VI60-600: **6.14947 progress/s**, minimum **6.04733**, fresh **0.81993/s**,
  raster59.846/convert17.640/other85.053 ms/VI, unchanged72draw/198skip.
  Evidence `fps8-alpha-rental-{target.log,measure.json}`. Battle remains pending.
- The force-inline native executor passes both matched VI120 full-state
  comparisons. Diagnostic warm other cost remains higher than interpreter
  (battle~198 ms versus matched short~154; rental~107 versus~82). Reject for player.
- Exact host-only PC profiling counts all31,109,273 battle instructions,
  including delay slots. No dominant idle loop: the hottest individual PC
  executes92,160 times (0.30%); its four-word cache-maintenance loop is bounded.
  Rental also has that bounded loop. Raw private diagnostic instruction samples
  remain under ignored `fps8-exact-{scene}/game.log`, never repository ROM data.
- Expanded CPU dispatch removes COP1->format dispatch layers and directly
  selects more original integer handlers. Full ARM32/high-resolution tests
  pass; matched SH4 game states are running. No qualified gain yet.
- A renderer comparison against8state-deps-base changed framebuffer CRCs,
  but that baseline's saved flags omit identity/cutout, while8state-batch-base
  enables both. It is a confounded comparison, not evidence isolating batch
  retention. Build8state-retention-base matches all flags except batching.
- Native store continuation is opt-in: larger blocks within2MiB, each aligned
  RAM store checks its actual byte/half/word lane against future code before
  continuing. Prefix freshness, hooks/MMIO exits and timing stay unchanged.
  First synthetic run failed30 old post-store expectations; returned prefixes
  were correct, but the fixture expected the old early exit register effect.
  Corrected expectations and42 explicit self-modification window cases are
  under test. No game/player approval yet.

- Matched retention-base versus batch-base confirms battle VI120 full state
  passes; rental differs in18 framebuffer/page/scanout rows while CPU/event
  bytes and other device/region hashes match. This isolates a batch-size
  dependent fast-renderer output difference. Do not claim framebuffer equality
  or approve a replacement player from rate alone. Exact visible RGB565 serial
  exports and a fail-closed RLE decoder are now being run to measure its size.
- Store-continuation CPU blocks pass both matched VI120 full states. Warm
  diagnostic other cost remains~192 ms battle/~105 ms rental, above original
  matched short~154/~82; still reject player. Explicit42 alias/width/code-overlap probes
  pass. Cached low-result reuse also passes1,024 arithmetic cases,80 memory,
  eight hook exits,136 cache cases and42 store windows on SH4. An aligned-word
  freshness check replaces general memcmp while retaining every-code-word
  validation; synthetic and paired game checks are running.
- Scanout decoder validates contiguous run indices, exact dimensions/pixel
  totals, positive bounded run lengths and unique ordered completion boundaries.
  Round-trip plus five truncation/repetition/overflow/misordering controls pass.

- Exact rental RGB565 exports (640x474,303360 pixels) isolate a batch-dependent
  difference:128-slot retention versus256-slot batching changes95825 pixels,
  max6 five-bit channel steps, mean RGB byte error2.83552. Against genuine SH4
  software, batch has142809 changed pixels, max18, mean5.31225; old retention
  has145107 changed, max18, mean7.73428. Lower mean does not establish accuracy.
  Evidence: `fps8-{batch,retention}-vs-reference-rental-pixels.*` and
  `fps8-batch-rental-pixels.*`. Player remains unchanged.
- Filtered integer rectangle sample centers (`RASTER_RECT_SAMPLE=1`) pass the
  two-scene SH4 ramp probe (max1 channel step). Revised integer scissor probes
  pass three GPU scenes. Strict/software replay passes; fast keeps the same
  four failures. Rental mean reference error falls to2.39501, max8,138102
  changed pixels, versus5.31225/max18 before. Eight pixels still have8-step
  error; general filtering/precision remains unqualified. Evidence:
  `fps8-center-{fast,strict,reference}-target.log`,
  `fps8-center-vs-reference-rental-pixels.*`.
- Lazy skipped-frame clip transforms (`RSP_LAZY_CLIP=1`) preserve the matrix
  snapshot at each vertex load; only CULLDL materializes skipped positions.
  Pool recycling/matrix changes and invalid cull endpoints pass ARM32/high-res.
  Both SH4 VI120 full states match alpha-base, including scanout. Rental cost
  is unchanged; battle raster cost saves roughly0.61ms/VI, while other cost
  changes154.048->154.159ms. Not a solution to8.
  First capture wrapped audit lines and failed the parser; repeated wider
  captures are complete and pass, without relaxing parsing.
- Local instruction/PC interpreter (`INTERP_LOCAL=1`) keeps current fetch/PC in
  C locals across ordinary integer instructions and aligned RAM accesses. It
  publishes both before every original handler; branch/delay, FPU, Count and
  MMIO/hook handlers remain original. Fresh fetches see CPU/DMA modifications.
  Full ARM32/high-res passes, plus1024 differential arithmetic streams,128
  memory/handler cases, two self-modifying alias cases and16 branch-delay cases.
  Paired SH4 VI120 full-state comparisons are running. No rate claim yet.

- Local-PC CPU path passes both SH4 game full states, but warm diagnostic other
  costs162.983ms battle/87.179 rental; slower than the matched short baseline
  154.043/82.212ms. Reject player. Do not compare VI60-120 CPU diagnostics to
  the different animation phases averaged across VI60-600 (136.7/85.1ms).
- Handwritten SH4 threaded loop (`INTERP_THREAD=1`) saves r8-r14/PR, uses no FP
  register/FPSCR instructions, reads current code every instruction and retains
  original branch/FPU/device handlers. Native differential1024 arithmetic,
  128 memory/hooks, two self-modifying aliases,16 branches pass. Both VI120
  full states match the corrected texel baseline, including scanout. Short
  other costs193.208/104.940ms: reject player. It is not a performance gain.
- `RASTER_TEXEL_SCALE=1` restores final rectangle combiner samples, corrects
  integer nearest unit sampling, and compensates PVR packed-channel expansion
  for unmodulated RGB565/ARGB1555 textures. Fractional ARGB4444 and varying shade
  retain their path. Full fast SH4 replay now passes all original groups,
  including combined cold/hot, all66 precision scenes and exact initial spans;
  evidence `fps8-texel-fast-target.log` with the then-current checker. Rental
  reference mean error2.07547/max8/126385 changed pixels,8 isolated8-step pixels
  remain. This is still an approximate renderer; not exact game-frame parity.
- STATE_AUDIT with FRAME_AUDIT has a nested image export. State checker now
  validates the complete nested RLE before comparing189 state rows. Frame
  checker rejects unknown/interrupted payload rows. Five new nested negative
  controls (truncated/repeated/interleaved/unknown/misordered) reject; no audit
  completeness or comparison requirement was weakened.
- Indexed texture lookup (`RASTER_CACHE_INDEX=1`, requires batch cache) hashes
  unchanged texture content once per TX mutation, hashes current descriptors
  and shader inputs every query, and uses512 collision chains with full key
  equality. Eviction order, texture residency and pinned flush rules remain.
  Two different TMEM contents intentionally collide under the full hash; cold
  and hot red/blue outputs exercise collision rejection. Initial fixture had
  host/TMEM byte order wrong and failed all three backends; corrected fixture
  passes all three backends after correcting the fixture. Both indexed game
  states match the corrected texel baseline. Short raster costs63.587->62.888ms
  battle and59.957->59.622ms rental; other154.074/82.298ms. This is a small
  diagnostic saving, not a qualified600-update gain. Evidence:
  `fps8-index2-{fast,strict,reference}-target.log`, `fps8-state-index-*-target.log`.
- Native-boundary attempt policy (`INTERP_BLOCK_FRONTIER=1`) attempts a block
  once after an original handler rather than every ordinary instruction and
  unsupported suffix. Fresh word/store overlap checks and original handlers
  remain. Both VI120 game states pass. Other172.911/94.617ms remains worse than
  matched154.043/82.212ms, despite reducing repeat attempts; reject for player.
- Deferred PC publication (`INTERP_PC_LAZY=1`) removes an observer-visible PC
  store only between ordinary instructions. Original handlers and nonordinary
  memory callbacks receive the fetched PC; debugger/compare observers reject
  this build option. ARM32 differential1024 arithmetic/128 memory-hook/two
  self-modifying/16 branch streams pass; both SH4 game states pass. Short other
  cost151.728/80.800ms versus154.043/82.212ms is the first useful recent CPU
  saving. Count, instruction budget and frameskip are unchanged. More exception
  coverage and isolated long runs are required before player promotion.
- The expanded differential PC-publication test adds64 raw FP transfer/handler
  cases across FR0/FR1 layouts, eight CU1-disabled faults and eight Count-wrap
  branch controls. ARM32 and native SH4 pass all1,250 streams; game-state parity
  has already passed. Evidence `fps8-pclazy-boundary-{test,target}.log`.
- Shared word-RAM proof (`INTERP_BLOCK_RAM_PROOF=1`) saves/restores r8 on every
  generated exit and proves both installed word handlers per virtual page.
  Proof reuse occurs only inside a prefix with no original handler/event/observer;
  width, alignment, zero address, fresh words and store-overlap checks remain.
  SH4 synthetic1,024 arithmetic/80 memory/eight hooks/136 cache/42 store-window
  cases plus24 same/different-page/width-hook/side-exit cases pass. Both game
  states match the corrected texel baseline. Other176.246/96.563ms regresses
  versus frontier172.911/94.617ms and interpreter154.043/82.212ms; reject for
  player. Larger generated guards shorten useful prefixes: battle13.78M native
  instructions versus14.48M previously, despite a similar3.07M cache hits.
- Updated SOFT_PROFILE separates software draw time after the GPU barrier from
  total draw-call time (GPU attempt/flush included). State keys now include alpha
  compare, depth source and color/depth image aliasing. `Soft path` rows identify
  remaining software calls/time without double-counting their preceding barrier.
  This is diagnostic only and compiled out of performance/player builds.
- Largest rental image discrepancies are the vertical edge at x84,y72..79;
  adjacent y71/80 have four-step error. An enlarged reference/candidate crop is
  `fps8-rental-edge-reference-candidate.png`. This localizes the next sampling/
  alpha-edge probe; no tolerance has been relaxed.
- Updated120-VI profile: rental software draws total2.469s (20.572ms/VI over
  the whole diagnostic), primarily TEXEL0*ENV coverage rectangles1.374s and
  ENV/PRIM interpolation0.588s. Battle software draws total about1.713s including
  0.438s in mux004197ff/fffffe38,0.419s in depth triangles and0.316s in constant
  shade triangles. These are post-barrier draw times; total call time and PVR
  waits/baking are nested and must not be added twice. Both profile states match
  corrected renderer state/checksums. Logs `fps8-audit2-*-target.log`.
- Uniform source-alpha proof (`RASTER_TEXTURE_ALPHA=1`, requires FULL_ALPHA)
  proves the current one-cycle alpha equation assuming TEXEL0 alpha255, then
  checks every raw source texel before allowing coverage. Baked/clamped alpha
  and stale COMBINED/TEXEL1 cannot establish opacity. Nine exact cold/hot,
  constant-alpha, transparent, fractional-IA and unknown-source fixtures pass
  on all three SH4 backends; fast executes four new GPU scenes. Both game states
  match indexed baseline, but short raster62.888/59.665ms is unchanged. It does
  not accelerate these game checkpoints and remains opt-in. Current full replay
  evidence `fps8-texalpha-{fast,strict,reference}-target.log` includes the new
  fixture; older logs cannot satisfy its current completeness check.
- Inline pure-core Count update (`INTERP_COUNT_INLINE=1`) retains every original
  boundary, unsigned delta, count_per_op and non-pure fallback. ARM32 full tests
  and1,250 differential streams pass; the scalar oracle forces the original
  external update_count path. Native SH4 also passes all1,250 streams, and both
  complete120-VI game states match deferred-PC baseline. Isolated600-VI runs
  of the combined candidate complete: battle4.82806/min4.52864,
  rental6.37671/min6.27985. Fresh draws0.64374/0.85023 per second. Both warm
  runs draw72/skip198 lists and fail8; keep player unchanged. Evidence
  `fps8-qual2-{battle,rental}-{target.log,measure.json}`.

## Code-generation validity inventory for the next CPU experiment

The proposed `INTERP_BLOCK_EPOCH` proof is scoped to one pure-interpreter call.
It does not assert that arbitrary external callers announce every RAM write.
Outside that scope, native blocks continue comparing fresh instruction words.

| Writer or observer | Current call ownership | Required proof invalidation |
|---|---|---|
| CPU SB/SH/SW/SD, unaligned merges, SC and FP stores | Every pure-core store uses the width macros in `platform/dc_interp_memory.h` | Increment the affected physical4KiB page versions after ordinary writes, including any crossed page and RDRAM wrap |
| Generated native stores | Inline generated RAM write; no callbacks inside a prefix | Increment physical page version, retain the existing future-instruction overlap exit; guard version wrap |
| PI ROM/save DMA; SP DMA; SI/PIF DMA | `gc_memory/memory.c` nonordinary installed handlers call `gc_memory/dma.c` | Invalidate all code proofs before and after every nonordinary memory callback |
| RSP HLE audio/JPEG/CIC and software/PVR framebuffer writes | `update_SP`/`doRspCycles` under nonordinary SP handlers; GPU flush can also occur at VI | Same callback invalidation, plus the interrupt observer boundary |
| VI flush/input/UI and checkpoint load | `gen_interupt` calls `updateScreen`/`new_vi` or `savestates_load` | Invalidate before and after every pure-core interrupt callback, including early-return paths |
| ROM boot/reset/checkpoint before entry; shutdown renderer work | Before/after the pure-interpreter lifetime | Reset on entry; disable scoped proofs on exit |
| AICA worker | Reads the private audio ring into AICA buffers; `AiLenChanged` copies RDRAM into the ring on the CPU thread | No asynchronous guest-RAM writer was found in `gc_audio/audio-dc.c`; future writers must disable or extend this proof |
| Debugger/compare-core/per-instruction observers | Optional unsupported diagnostics | Reject this experimental option with those observers |

KSEG aliases must use the same physical versions. A changed observer generation
requires full word validation, never unconditional reuse. Page or observer wrap
must disable the shortcut until reset, including a wrap during generated code;
it must not clear/free the currently executing block. Tests must cover CPU
widths, aliases, two-page code, raw DMA-style callback writes, outside-context
unannounced writes, self-modifying generated stores, wrap and re-entry. This
inventory authorizes an opt-in experiment only; its correctness and speed
remain unverified until those tests and paired game states pass.

The first scoped-generation prototype passes native1,250 CPU streams,40 writer/
wrap probes and battle120-VI state, but regresses short other cost to201.833ms.
It updates data-only page versions and rewrites three proof words on every hit;
do not promote it. Revised code marks only cached-code pages, increments their
versions inline at CPU/native stores, captures proofs only after fresh validation,
and skips the redundant native-prefix PC store under deferred publication.
Full ARM32 tests, native1,250 streams and41 writer/wrap probes plus42 active
self-modifying-store windows pass. Both complete120-VI game states also match
Count-only baseline, but other199.003ms battle/110.038ms rental remains worse.
Reject for player. Tracking code freshness alone does not repair this native
executor's short prefixes, generated memory guards, ABI and guest-register costs.

The next renderer probe (`RASTER_CVG_POINT`) targets observed identity coverage
rectangles. The current software model discards final alpha<32; exact integer
unit samples permit baking this cutoff into a binary mask, then using source-
alpha blending to implement accepted identity overwrites. Fixed alpha comparison
uses the larger cutoff; dither, real blending, depth, fractional origins/steps,
tile shifts and unknown next-texel inputs retain software. This is parity with
the port's existing coverage model, not complete N64 subpixel coverage emulation.
Fifteen exact image cases cover binary/fractional source alpha,31/32 thresholds,
hot unused state, IA8, alpha comparison, transparent destinations, overlapping
pending masks and fallback controls. Accepted pixels bake alpha255; rejected
pixels bake alpha0. A written-pixel bitmap limits readback to accepted pixels,
preserving destination RGB and alpha in holes without importing a backdrop.
The first implementation retained fractional accepted alpha and failed104 pixel
checks; its failing control is retained. All three complete SH4 replay suites
now pass; fast executes11 new coverage GPU scenes. Evidence
`fps8-cvgpoint-{fast,strict,reference}-target.log` (before the later span fixture).
Both complete120-VI game states match Count-only baseline. Short battle raster
60.719ms is unchanged, while rental66.538ms versus59.762ms regresses; reject
for player. Existing general-triangle approximation remains.

The optional `INTERP_DELAY_ALU` dispatch calls the original bodies directly for
common arithmetic and eight RAM-width operations in branch delay slots. It
leaves opcode fetch, branch operand capture, Count and interrupt boundaries
unchanged. Expanded tests cover256 streams across BEQ/BNE/BEQL/BNEL, taken/
untaken/annulled slots, branch-operand writes, MMIO hooks and CU1 faults. The
differential harness now snapshots and restores the serialized interrupt queue;
the initial missing queue caused a test-only crash and has been corrected.
Test-only `DC_INTERP_ORACLE` forces the original external Count and table-dispatch
paths while preserving pure-core exception context, and is absent from game
builds. Full ARM32 regressions and all1,506 differential streams pass on ARM32
and native SH4. Both complete120-VI game states match Count-only baseline.
Short isolated battle other150.425ms versus145.707ms and rental79.970ms versus
76.538ms both regress, so reject for player. A concurrent correctness run is saved
as `fps8-state-delay-rental-overlap-target.log` and excluded from timing.

An exact software identity rectangle span (`SOFT_IDENTITY_SPAN=1`) hoists
framebuffer/depth/destination-read qualification out of the pixel loop, retains
the scalar TX/CC order and fixed alpha cutoffs, and restores BL temporaries from
the last accepted pixel. It rejects real blending, depth and dither; flipped
rectangles retain scalar handling. Sixty-four whole-frame/13-register-byte
comparisons against original per-pixel handlers pass on ARM32 and SH4, including
alpha0/31/32/63/64/128/254/255, rejected final samples, fractional filtering and
fallback controls. Full ARM32 regressions and native software/fast/reference
replays pass. Both120-VI game states match Count-only baseline. Battle is unchanged;
rental raster50.236ms versus59.762ms improves, short progress6.91117 versus6.49135.
The isolated600-VI combined checks complete: battle4.82849/min4.52864,
rental6.77731/min6.66448, fresh0.64380/0.90364 per second. Both warm540-VI
runs draw72/skip198 lists and fail8; no player promotion. Logs/JSON/12-artifact
receipt: `fps8-qual3-*` under ignored validation output. The earlier build logs
for qual2/epoch2 were overwritten by copied-script redirects; their saved ELF,
compiler flags, game logs and manifests remain authoritative and verified.

Native executor diagnostics (`INTERP_BLOCK_PROFILE=1`) pseudorandomly sample
about one in1,024 attempts using the existing KOS nanosecond clock, with no timer
reconfiguration. They separate qualification, lookup, freshness/epoch validation,
compilation and native execution, and report completed-prefix distribution and
resident code use. The timer's64 consecutive read pairs cost17,920ns in this
Flycast run; all stage times include instrumentation and are diagnostic only.
Battle120 passes exact state comparison. Of8,772,651 attempts,5,639,536 execute
zero instructions;3,133,115 nonzero prefixes retire13,641,706 instructions,
about4.35 each. Sampled means: qualification0.539us, lookup1.047us,
validation0.926us, compilation28.794us, native1.236us. These are nested only
within their sampled attempts, not separate whole-run costs to add to PERF.
Resident1,677 slots contain485,368 generated bytes but reserve1,287,936 code
bytes; metadata/cache total1,761,280 bytes. Source/layout overhead affects this
diagnostic, so its timing cannot qualify a player. Evidence
`fps8-state-blockprofile-battle-target.log`.
Rental also passes complete120-VI state. Of5,181,725 attempts,3,364,562 execute
zero;1,817,163 nonzero prefixes retire7,492,513 instructions (about4.12 each).
At1,540 resident slots,442,584 used code bytes occupy1,182,720 reserved bytes.
The same overhead pattern appears in both scenes.

The next measured-mechanism CPU probe (`INTERP_BLOCK_DISPATCH=1`, requires
BLOCK/FRONTIER) moves native eligibility after the existing fresh fetch. A small
opcode mask rejects unsupported first/second words before the external executor
call. Full block validation, RAM handlers, writer epochs, budgets and original
branch/interrupt paths remain. It targets the observed zero-prefix calls rather
than expanding generated opcode coverage. Full ARM32 regressions/native1,506
streams and both120-VI game states pass. Short other184.318/101.354ms improves
the epoch prototype's199.003/110.038ms, but still regresses against the actual
Count-only baseline145.707/76.538ms. Reject for player. The mechanism removes
measured overhead but does not make this architecture faster than interpretation.

The next architecture probe is `INTERP_BLOCK_ARENA=1`: compact four-way metadata
holds relocated-code pointers, while aligned code/literal pools consume actual
compiled size from the same512/1024/2048KiB total allowance. Metadata, temporary
builder storage, writer epochs and code share a compile-time checked budget.
The code pool is bounded and allocation-free. Wrap globally evicts cache metadata
before reusing executable bytes; compilation occurs outside native execution.
No links/pointers escape to guest memory, and direct block links remain future
work. Each copied block retains32-byte alignment, literal reach is checked, and
the installed code receives SH4 instruction-cache synchronization. The probe
also allows512-word build capacity and more compact metadata slots, so measured
changes must report prefix counts/cache use as well as speed.
Tests include8,192 distinct executed programs forcing arena wrap, reused virtual
addresses/KSEG aliases with patched words, bounded storage and reset/re-entry,
in addition to all memory/hooks/self-modifying/writer/CPU streams. Full ARM32
regressions pass. Native1,506 streams and all existing arithmetic/memory/hooks/
self-modifying/writer tests also pass. The8,192-program relocation stress executes
an actual wrap with a1,715,072-byte pool and376,832-byte metadata table. Alias
patching after reuse and reset/re-entry pass. Battle120 state matches baseline;
short other169.604ms improves gate184.318ms but remains slower than Count-only
145.707ms. Battle native instructions rise13,641,706->14,777,512 and compiles
fall55,320->17,440. The arena wraps three times in that game run. Rental120 also
passes exact state, but short other91.683ms remains worse than baseline76.538ms
(gate101.354ms); progress5.91510. Reject for player. Compact allocation repairs
waste/cache pressure but does
not by itself eliminate per-prefix lookup, guards, ABI and register traffic.

The next interpreter probe is `INTERP_BRANCH_PAIR=1`, separate from native/local/
threaded/profile variants. BEQ/BNE/BEQL/BNEL can execute their original simple
integer delay bodies directly from the main loop. Eligibility is checked before
effects: aligned ordinary RAM, a fresh in-range delay word, no existing delay,
and no self-target/idle shortcut. Likely annulment reads no delay word. Captured
64-bit operands, local branch globals, final opcode/PC, Count boundaries and
interrupt delivery remain exact. Memory, FP, faulting and unsupported slots fall
back before effects. The existing256 branch-delay streams plus game CPU/event/
memory states must pass; passing correctness alone does not qualify performance.
This targets the measured3.12-million conditional branches in120 battle updates;
its benefit is unverified until isolated matched runs finish.
Full ARM32 regressions and the expanded1,634-case CPU oracle pass on ARM32
and native SH4, including128 extra negative/idle, high-half operand, final-RAM
word/annulment and due-Compare boundaries. Both120-VI game states pass. Short
battle other152.191ms and rental80.972ms regress against145.707/76.538ms;
reject for player. The main loop grows0x6d0->0x9d8 bytes on SH4. The candidate
eliminates calls but increases hot code/dispatch work. Raw logs are preserved.

The next renderer probe, `SOFT_COMBINE_SPAN=1`, requires identity spans. It
specializes only TEXEL0 times packed ENV/PRIM with zero RGB subtract/add terms
and independent TEXEL0 alpha or TEXEL0-alpha times packed ENV/PRIM/ONE alpha.
RGB retains the original multiply-then-divide order; only the constant alpha
factor is divided once. Texel samples still execute in scalar order, including
rejected pixels. The final TEXEL0/replicated-alpha registers are restored from
the last sample, separately from the last accepted BL pixel. Feedback, TEXEL1,
other muxes and unsupported blending retain original handling. The scalar
fixture expands64->320 cases with nonuniform RGB, ENV/PRIM multiplication,
direct alpha and COMBINED feedback controls across the same thresholds/filter/
depth/blend/dither/flipped cases. Full ARM32 and three native suites plus both
game-state comparisons and matched timing are required before promotion.
All320 scalar cases and the full ARM32 replay pass. Native software, fast and
reference suites also pass, including comparisons with the same-SH4 reference.
Both game-state/timing checks remain pending; no player promotion.
The first span version passes both120-VI states. Battle remains unchanged,
while rental short raster48.906ms versus50.236ms improves progress6.97529.
Full ARM32 emulator regressions/native1,634 CPU cases pass. A follow-up retains
the original final RGB/alpha zero additions for raw signed-zero parity;4,096
fractional/signed-zero combiner values and temporary states match original on
ARM32, with all4,096 specialized calls exercised. Three native suites and both
game states must be requalified for this revision (`fps8-combinespan2-*`). The
earlier320-case logs remain historical evidence and lack the newer value fixture.
The600-VI `8qual4` package was built from the first version but never measured;
do not treat it as qualified or overwrite qual3 evidence.

`INTERP_BLOCK_HOT_ENTRY=1` is the next CPU lookup probe, requiring ARENA/EPOCH/
DISPATCH and excluding sampled profiling. A bounded direct table stores one
validated native entry per hashed PC, with its exact tag/count, observer/page
generations, code/raw pointers and age location. An inline hit checks the same
proofs/budget before calling native code and preserves partial/zero side exits;
misses use the original four-way compiler/validator. Metadata replacement clears
the affected hot reference before compilation; arena wrap/reset clears all hot
references before executable bytes can be reused. Age wrap and disabled/outside-
scope proofs take the original path. The table's128KiB at2MiB configuration is
included in the same total budget, reducing available code capacity. It creates
no native-to-native links and changes no Count/branch/interrupt semantics. Native
tests must execute hot hits, writer/observer/page/age/reset/reentry misses and
guarded zero exits; relocation stress checks global hot eviction on actual wrap.
Full CPU/state/performance qualification remains pending.
Full ARM32 regressions/native1,634 CPU streams pass. Native arithmetic/memory/
hooks,41 writer probes,42 active store windows,8,192 relocated programs with an
actual arena wrap, and15 direct hot-entry controls pass. The initial eviction
fixture used128-byte candidate strides that generated zero collisions; its
failing log/ELF are retained, and4-byte candidate search now exercises actual
metadata replacement. Pool capacity1,584,000 bytes reflects the128KiB table.
Battle120 state passes, but short other171.424ms is worse than arena169.604ms
and interpreter145.707ms. Rental120 also passes exact state, but other92.892ms
regresses versus arena91.683ms and interpreter76.538ms; progress5.86240.
Reject for player. An extra direct table and inline proof checks do not repair
the architecture's short prefixes/guard/register/ABI cost in these scenes.

The final combiner revision also passes all three complete native suites and
the4,096 raw value/state controls (all specialized calls exercised), plus full
ARM32 emulator tests and1,634 CPU streams. Revised game states and600-VI
qualification remain pending; historical initial span logs are not substitutes.

`INTERP_COMPACT=1` is an isolated compiler-layout probe: GCC's size optimization
applies only within `pure_interp.c`; renderer/audio choices and runtime timing
remain unchanged. It excludes alternate dispatch/cache/profile experiments and
uses DIRECT/MEMORY/ALU with deferred PC/inline Count. Larger hot interpreter
loops have repeatedly regressed, so this tests the converse mechanism before
more opcode expansion. Compiler flags record the opt-in macro; compare native
text/main-loop sizes, full ARM32/native1,634 CPU oracle streams, both exact120-VI
game states and matched short timing. Only a measured win can join qualification.
