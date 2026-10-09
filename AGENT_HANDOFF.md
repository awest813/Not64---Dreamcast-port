## Latest work: frontend checkpoint, user requested stopping (2026-10-04)

User expanded scope to fully functional, polished Dreamcast UI/UX and menus,
then explicitly asked to reach a stopping point. Work stops at a built/tested
frontend checkpoint; complete game/SD/PVR qualification is still pending.
Read `tools/dc/UI_UX.md` for implemented behavior and the remaining checks.

- New menu-first player: `build/dc/not64-ui.cdi`,
  `build/dc/Play-Not64-UI.cmd`; distinct from all qualified Stadium 2 players.
- Shared TV-safe UI, folders/source switching/rescan, launch validation,
  settings with honest capabilities/storage feedback, persisted controller
  mappings, pause settings/controls/slot UI and confirmations.
- Interactive sessions are unlimited by default. Reset reloads the current
  ROM; Return to games works for disc and argv launches. Runtime errors recover
  to browser. Menu input is raw and held input cannot select the next screen.
- Temporary-file replacement protects previous settings, cartridge saves and
  v3 states from normal write/close failures. Failed native save keeps pause
  active; disable auto-save to explicitly leave without saving.
- Pause borrows/restores the displayed framebuffer after draining PVR, preserving
  display mode and caches. Original aspect follows the prior fast path; full
  screen/wide TV affect presentation only. These need actual game QA.
- Standalone frontend contracts pass via `tools/dc/run_ui_host.ps1`. This
  harness uses real menu/controller/settings/file code and stub core services.
  The Docker core suite remains unavailable (Docker Desktop engine error).
- Native GCC 15.1/KOS 2.3/LTO builds/package pass. Flycast boot/browser layout
  and folder opening observed. Full screen navigation, actual SD save/load,
  pause/resume/framebuffer/cache safety and fresh Stadium 2 speed/graphics
  measurements remain pending. Do not claim full polish or transfer old speed
  results to the UI build. Final idle redraw adjustment is not game-qualified.
- Source backups: `build/dc/validation/ui-before-source/`; logs/provenance:
  `build/dc/validation/ui-*`. No commit/push. Existing dirty work retained.
- The dedicated Flycast test process has been closed. No emulator remains
  running from this UI pass.

## Previous follow-up: filtered texture accuracy with preserved speed (2026-09-30)

Continued the user's graphics-accuracy request for Pokémon Stadium 2, retaining
the clarified 3.8 game-progress updates/s objective and steady mode7. No
increase in frameskip. Historical higher FPS objectives remain unachieved.

- Opt-in `RASTER_FILTER_GRID=1` / replay `FILTER_GRID=1` requires triangle
  origin and shade scale. Three supported shade equations with three-point
  filtering and native padded textures <=64x64 bake quarter-texel samples.
  Logical UV dimensions stay unchanged; physical center follows the grid.
  Rectangles, point/average filters, large textures and binary cutout shade
  retain native grids. No additional GPU eligibility. Factor joins cache key.
- Four complete optimized LTO native suites pass (`accuracy-grid7`,
  `grid7-base`, `grid7-strict`, `grid7-reference`). Twelve-case filter probe:
  8 cold/hot texture cases, all three equations, fractional/perspective UVs;
  4 complete cold/hot framebuffer guards. Error 7453->754 (89.8833%), peak
  16->4 five-bit steps under unchanged bound5; 8 grids, 48 completed scenes,
  hot reuse. Current checker requires unique identity and all cases/fixture;
  nine negative controls reject incomplete/disabled/repeated/failing evidence.
  Older native logs cannot qualify this current checker.
- Battle VI120 mean RGB-byte error 20.697736->20.644967 (0.25495%), changed
  pixels 65289->65100/75840, peak26 remains at5 pixels. CPU/event bytes and
  MMIO/SP/PIF hashes match prior qualified player and software reference.
  Rental every pixel and entire audited state match prior Accurate-Origin.
  Final optimized grid7 images match grid6 exactly. Battle at120: 8 bakes,
  VRAM126080->329600 (+198.75KiB), same65 misses, no extra evictions/flushes;
  rental0 bakes and403968bytes. Existing2MiB cache/scratch budgets unchanged.
- Rejected half-texel grid reached9 steps and battle error26.217506. Preserved
  grid1–grid4 diagnostics; first probe had two wrong shader selectors, fixed
  before qualification. Grid5/6 images exact. Initial full candidate rental
  average slowed0.3508% with no grids; rejected under0.2% preservation bound.
  Removed factor divisions from native texel/triangle paths; only active grid
  uses exact power-of-two scaling. Divisions source snapshot and timing logs
  remain diagnostic; original failed performance ELFs/CDIs were rebuilt.
- Final isolated VI60–600, 9 windows, freshly remeasured rebuilt baselines:
  battle baseline 4.825041/min4.523280, actual unlimited player
  4.823754/min4.520649; rental baseline 6.803749/min6.679308,
  corrected 6.796910/min6.680265. Avg/min within0.2%, all windows>=3.8;
  fresh draws 0.643167/s battle, 0.906255/s rental. Same72 drawn/198 skipped,
  mode7 skips2,3,3,3 max3/avg2.75. First60 cold progress 4.019075/6.268425, also>=3.8.
  Still checkpoint-only Flycast coverage, no completed attack/all-scenes/real DC.
- Qualified separate player `build/dc/Play-Stadium2-Accurate-Filter.cmd` ->
  `not64-stadium-accurate-filter-gameplay.cdi`, live controls, battle checkpoint,
  no scripted input/automatic Start/frame-step limits. All three previous
  players and their original recorded artifacts/evidence hashes preserved.
  `accuracy-filter-manifest.json` stores artifact/flags/log/source provenance;
  `accuracy-filter-source` is the optimized snapshot. All validation Flycast
  processes closed with verified native paths and WaitForExit.
- Updated main/replay build options, raster cache/bake/UV code, focused native
  fixture, fail-closed checker and graphics/replay docs. No commit or push.

---

## Latest follow-up: continue graphics accuracy while retaining speed (2026-09-30)

Completed the user's "Keep going" follow-up with an opt-in triangle attribute
sampling correction and a separate qualified live-control player. The prior
Steady and Accurate player artifacts remain byte-for-byte preserved. The
historical 8 FPS objective remains unachieved; keep the clarified progress
metric and steady mode7, without increasing frameskip.

- `RASTER_TRI_ORIGIN=1` requires `RASTER_TEXEL_SCALE=1`. New helper
  `platform/dc_raster_origin.h` shifts U/W, V/W and reciprocal-W together, then
  recovers perspective UVs; supported RGB shade planes shift likewise. Vertex
  x/y and shade alpha stay unchanged. Degenerate/nonfinite geometry or
  attributes, nonpositive shifted reciprocal-W and unrepresentable RGB retain
  the previous GPU attributes. Rectangles explicitly bypass this correction.
  No additional texture allocation, scene flush or software fallback.
- Four complete LTO native suites pass: corrected/baseline fast, strict and
  independent same-SH4 software, plus corrected non-LTO. New fixture: 40
  independent barycentric projection/rejection cases, four complete framebuffer
  footprint comparisons, four exact interior texture-ramp cases. Enabled GPU
  build executes seven corrections and 12 completed scenes. Existing varying/
  perspective shade error improves 55->34, maximum two->one five-bit steps.
  All replay logs now require the triangle-origin fixture and build identity;
  checker `--tri-origin` requires the enabled fast GPU path. Six negative
  controls reject disabled, absent, truncated, repeated or deliberately failing
  evidence. Replay Makefile adds `LTO=1`. Software-only prints no GPU counters;
  the initial zero-counter diagnostic is retained as `*-diagnostic0.log`.
- Battle VI120 versus the same software image: mean RGB-byte error
  23.013783->20.697736 (10.0637%), changed pixels 65803->65289 of 75840,
  maximum remains 26 steps. 21133 pixels improve, 15437 worsen and 39270
  have equal error; every horizontal band improves in aggregate. CPU/event
  bytes and MMIO/SP/PIF hashes match the prior fast/reference. Counters at
  120 submissions: 2455 corrected / 910 guarded. Rental VI120 image and
  entire audited state remain exact to the earlier Accurate build; zero
  triangle corrections, mean reference error 2.075474 and maximum 8 steps.
- Initial shared array/index submission passed older native fixtures but
  corrupted rectangle UVs in game captures (battle mean 40.5359, rental
  17.9249; maxima 31). Rejected. Restoring the original floating-argument
  submission for rectangles/guarded triangles resolves this; no compiler
  cause established. `accuracy-origin1-*` keeps rejected exports;
  `accuracy-origin2-*` keeps qualified images/native evidence/source snapshots.
  The older geometry-shift experiment remains rejected independently.
- Matched isolated VI60–600, nine 60-update windows: baseline battle
  4.828063/min4.528632; actual new unlimited player 4.825477/min4.523288.
  Rental baseline 6.799471/min6.679311; corrected 6.800327/min6.679304.
  Average/minimum retain baseline within 0.2%; every window exceeds 3.8.
  Same 72 drawn / 198 skipped lists, mode7 skips 2,3,3,3, max3/average2.75.
  Fresh draws 0.643397/s battle and 0.906710/s rental. Not all-scenes,
  completed-attack or physical Dreamcast qualification.
- Cleanup initially compared mixed slash/backslash paths, leaving two idle
  emulators beside the first player timing run and the running player beside
  a partial rental baseline. Excluded/retained `*-overlapped-*` and
  `*-rental-aborted-target.log`. Fixed cleanup uses the resolved native path,
  waits for termination and verifies no emulator before each subsequent run.
  Fresh isolated player reproduces the earlier timing exactly; only isolated
  captures qualify. All validation emulator processes are now closed.
- New player `build/dc/Play-Stadium2-Accurate-Origin.cmd` ->
  `not64-stadium-accurate-origin-gameplay.cdi`: battle checkpoint, live controls,
  no scripted input, automatic Start, frame/step limit. Old player hashes
  verified against both original manifests. Ignored qualification/build scripts,
  full serial/state/frame logs, PNG/JSON comparisons, source snapshot and
  `accuracy-origin-manifest.json` preserve flags and SHA256 provenance.
  The new player build initially overwrote the prior `accuracy-player-build.log`
  and package log; its output is copied to `accuracy-origin-player-*`, and the
  build script now uses those filenames. Prior compiler flags/hashes preserved.
- Updated `tools/dc/STADIUM2_GRAPHICS_ACCURACY.md`, `RASTER_REPLAY.md` and
  Makefile help. No commit/push; earlier unrelated dirty files preserved.

The previous graphics correction and earlier work history follow.

## Prior request: graphics accuracy while retaining speed (2026-09-30)

The latest user request focuses on graphics accuracy while keeping the speed
improvements. Completed a bounded shaded packed-color correction, with native
and game-image evidence, and a separate qualified live-control player. The
historical8 FPS objective below remains unachieved; this turn did not increase
frameskip or claim overall game fidelity/performance.

- New opt-in `RASTER_SHADE_SCALE=1` requires TEXEL_SCALE/SHADE. Corrects the
  multiplicative RGB of four separable shade equations; bias/alpha, texture
  memory/cache/batches unchanged. Fractional ARGB4444 unchanged.
- Four full current native suites pass: baseline/corrected fast, strict and
  independent SH4 software. New128-case cold/hot nonuniform RGB ramp: total
  channel error1104->460 (58.3%), max1step. Existing affine/perspective probe
  75->55, max2steps. Exact checks stay exact. All replays now require shade-scale
  build identity and shaded-precision fixture; checker `--shade-scale` asserts
  enabled GPU capability. Four missing/disabled/truncated/repeated controls reject.
- Initial128-case hot-blend oracle compared first draw to the second reference;
  final separate cold/hot reference storage fixes this. `*-oracle1.log` retained.
- Matched isolated VI60-600: baseline battle4.828490/min4.528653; actual corrected
  unlimited player4.828486/min4.528641. Rental baseline6.799481/min6.679331,
  corrected6.799471/min6.679328. Same72drawn/198skipped lists, steady mode7,
  average2.75/max3skips. Rates retain baseline within0.001%. Fresh drawings
  0.643798/s battle,0.906596/s rental. Not all-scenes/attack/physical-DC coverage.
- Battle VI120 versus independent SH4 software: mean RGB-byte error
  23.42851->23.01378 (1.77%); corrected65803/75840 changed pixels, max26steps.
  Exact CPU/event bytes and MMIO/SP/PIF hashes match across all three captures.
  Rental VI120 entire state and image match prior fast exactly; still mean2.07547
  versus software, max8 at x84/y72-79. Remaining image differences are material.
- Half-pixel triangle-origin experiment passed current native suite and reduced
  battle mean to20.30005, but max26->28 at two pixels due coverage shifts. Rejected
  and removed from production source. Ignored `accuracy-triangle-*` source/ELF/CDI/
  serial/frame evidence retained. Next sampling work should preserve geometry and
  correct interpolated attributes; do not revive the geometry shift blindly.
- New separate player `build/dc/Play-Stadium2-Accurate.cmd` ->
  `not64-stadium-accurate-gameplay.cdi`: battle checkpoint, live controls, no replay,
  automatic Start, frame/instruction limits. Old steady ELF/CDI/launcher hashes
  still match original manifest. No emulator validation processes left running.
- Report `tools/dc/STADIUM2_GRAPHICS_ACCURACY.md`; updated RASTER_REPLAY docs and
  Makefile help. Ignored `accuracy-*` logs/JSONs/PNGs/flags/hash manifest preserve
  evidence. Original8qual5 build log was overwritten during player build-script
  preparation; baseline compiler-flags/ELF/CDI remain unchanged. Corrected player
  build output copied to `accuracy-player-build.log`; script now uses that path.
- No commit/push. Existing unrelated/earlier dirty files preserved.

The earlier8 FPS audit and work history follow unchanged.

## Active request: 8 game-progress updates/s (2026-09-30)

The user asked to keep working until 8 FPS and requested a full emulator audit
and thorough improvement plan. The metric inherits their explicit earlier
clarification: VI game-progress updates, with fresh draws reported separately.
Keep steady mode 7 (maximum three skips, average 2.75); do not raise skipping.
The old 3.8 player below remains qualified and preserved. **8 is not achieved.**

Full audit and work plan: `tools/dc/STADIUM2_8FPS_PLAN.md`. Best isolated
600-update combined candidate: rental6.77731/min6.66448,
battle4.82849/min4.52864 (corrected texel renderer, indexed cache, deferred PC,
inline Count and software identity span). Logs/JSON `fps8-qual3-{battle,rental}-*`; aborted initial qual1 battle capture is
explicitly incomplete and excluded. Both use mode7
and draw72/skip198 lists in warm540 updates; **8 not reached**. All new graphics
caps are opt-in, and the preserved3.8 player is unchanged. Corrected packed-texel
fast renderer now passes the full SH4 synthetic suite, including COMBINED,
precision, spans and deliberate full-hash collisions; strict/software pass.
Rental VI120 game image remains approximate (mean RGB byte error2.07547,
max8 five-bit steps/eight pixels at that maximum). State/checksum comparisons
must use saved matching flags; historical four-failure records below are older.

Native CPU variants and expanded dispatch pass paired game-state checks but
remain slower in short diagnostics. Longer native blocks with per-store future
code overlap guards, reuse and boundary-only attempts pass but regress. Local
and handwritten threaded loops also pass states and regress. Matched short
VI60-120 baseline other154.043ms battle/82.212ms rental; do not compare these
to long VI60-600 animation phases (136.7/85.1). Deferred PC publication passes
ARM32 and both SH4 state comparisons, other151.728/80.800ms. Added64 FP transfer/
hook, eight CU1 fault and eight Count-wrap controls pass ARM32 and native SH4.
Dynamic word-RAM proof reuse passes SH4 synthetics and both game states but
regresses (other176.246/96.563ms); default off. Updated software/GPU path
profiling passes paired states and ranks remaining fallback costs. Uniform raw
texture-alpha proof passes all three full SH4 replay suites and paired game
states, but leaves these checkpoint raster costs unchanged; keep opt-in.
Inline Count updates pass full ARM32 tests, native1,250 streams and both states.
Scoped code-page generations pass full ARM32/native boundary checks and both
120-VI game states, but regress other199.003/110.038ms; reject for player.
Writer/observer inventory and wrap safeguards are in the plan. Broader delay
dispatch passes both states and full ARM32/native1,506 streams but regresses
short other150.425/79.970ms versus145.707/76.538ms; reject for player.
Test-only oracle routing preserves pure-core exception context, independently
uses original Count/dispatch, and snapshots/restores the interrupt queue.
Integer identity coverage masks pass the full fast/strict/software SH4 suites:
15 exact cases,11 new fast GPU scenes, transparent holes and overlapping masks.
Initial fractional-alpha failure is retained; both game states match but rental
raster66.538ms versus59.762ms regresses; reject for player. Exact software
identity spans pass full ARM32 and native software/fast/reference replays,
64 framebuffer/temporary-state scalar comparisons and both complete states.
Short rental6.91117 versus6.49135 improves; battle unchanged. Combined600-VI
qual3 completes both scenes, improves rental6.77731/min6.66448 and fails8.
Twelve artifact hashes saved; all old qualified3.8 and qual2 hashes still match.
Preserved3.8 player remains unchanged. Bounded code arena passes native relocation/
wrap/writer tests, full ARM32 regressions and both game states, but short other
169.604/91.683ms still regresses; reject. Branch-pair probe passes full ARM32,
native1,634 CPU streams and both states but other152.191/80.972ms regresses;
reject. Tests now include128 additional negative/idle/high-half/final-RAM/IRQ
branch boundaries. New simple-combiner span passes all320 scalar/framebuffer/
temporary-state cases and full native software/fast/reference suites. Rental120
state passes, raster48.906ms versus identity-span50.236ms, progress6.97529;
battle also passes, unchanged. Final combiner revision retains signed-zero
additions and passes320 framebuffer/13-color-state cases plus4,096 raw fractional/
signed-zero values on ARM32 and all three native suites; full ARM32/1,634 CPU
streams pass. Rental120 state passes, raster49.779ms/progress6.93512; final battle
and long `8qual5` pending. `8qual4` was built from the initial version but never
measured. Guarded hot-entry cache passes all native tests and both states but
other171.424/92.892ms regresses; reject. Its128KiB table shares2MiB, clears on
metadata/arena/reset reuse, no direct links. Initial eviction test's128-byte
stride produced zero collisions; corrected4-byte search and15 hot controls pass.
CPU-only `INTERP_COMPACT=1` compiler-layout probe is built; full ARM32/1,634
streams pass, native/state/performance pending. No8 player or commit/push.
Native cost sampling passes both states and identifies64% zero-prefix attempts,
lookup/validation overhead and fixed-slot waste. New DISPATCH gate moves the
unsupported-start check to the existing fetch. Full ARM32/native1,506 streams
and both states pass; other184.318/101.354ms improves epoch cost but remains
worse than Count-only145.707/76.538ms. Reject for player; broader architecture
work must reduce cache/ABI overhead and guest-register traffic.
Exact host PC
profiling shows no dominant idle loop. ARM block tests do not execute SH4 code.
Untimed correctness runs may overlap; performance runs use one Flycast process.
The current replay checker requires all new texture/alpha/scissor fixtures;
older logs may not satisfy it. No qualified8 player, no commit/push.

# Agent handoff — Not64 Dreamcast port

## Revised performance request qualified — 2026-09-29

User selected Pokémon Stadium 2, revised 20/15 to **steady 3.8 game-progress
VI updates/s with less frameskip**, explicitly choosing progress over fresh draws.
The revised request passes the two representative scenes. Old stored goal remains
paused with obsolete 20 FPS text; do not claim that objective was met. No subagents.

Final mode 7 warm VI 60–600, nine 60-update windows, Flycast 2.6/OpenGL:
- Battle: actual unlimited live-control player 4.284973 avg / 3.827127 min,
  fresh 0.571330/s. Raster88.493, convert 8.141, other 136.680 ms/VI.
- Rental menu:3.959935 avg / 3.919644 min, fresh 0.527991/s.
  Raster149.742, convert 17.657, other 85.072 ms/VI.
Logs/JSON: build/dc/validation/fps38-steady-{gameplay,rental}-{target.log,measure.json}.
All windows pass3.8. The actual player continued beyond600 without diagnostic
limits; its task-owned qualification Flycast is closed after saving results.

Deliverable: build/dc/Play-Stadium2-Steady.cmd launches the separate
not64-stadium-steady-gameplay.cdi. Unlimited frames/steps0, Start0, no input replay,
live controls, resumes battle-commands checkpoint. Compiler flags, boot equality
and image/ELF/launcher hashes verified; manifest fps38-steady-artifacts.json.
Existing user images/launchers preserved. No commit/push. Physical Dreamcast
and whole-game timing unverified; full fast renderer pixel accuracy unqualified.

Mode 7 steady repeats skips 2,3,3,3, avg2.75/max3, four draws per 15 lists. Every
rolling 30-list interval has8draws. CLI steady / Settings2 /3 /3 /3. Mode 6 balanced
(draw phase0/3 per7, avg2.5) remains available; old battle4.2842/min3.8336.
Modes0–5 fixed meaning unchanged; defaultoff. Main/Make clamps now7. Full ARM/
highres suites PASS, every FullSync, lifecycle, rolling draw count/max skip gap,
Settings roundtrip7. Perf parser labels steady-2-3-3-3; rejects8/changed policies.
Rental120 and battle-menu360 CPU/event tails match no-skip. Host PNGs intact;
not proof of a completed attack/battle or target GPU screenshot.

Retained work: exact qualified RAM8width handlers, ALU/memory computed dispatch,
fresh main/delay RAM fetch, forced common-memory inlining; packed framebuffer
import, unused TEXEL1 sampling elision with final state preserved, exact identity
blender; skipped RSP attributes/triangles while preserving CULL positions, index
errors, state and interrupts. Branch-inline/O3 experiments removed. Retained CPU
paths pass no-skip image/full-state parity. Packed integer/half-row RGBA16 VI
uses channel-exact rounded RGB565 pair averages; byte-safe unaligned/general
fraction fallback.65536-code partner oracle +96fraction/crop/stride cases PASS,
rental120 image and entire state equal baseline. Per-field cache tests both fields,
new render tokens, CPU callbacks, mode/lifecycle; little rental reuse by itself.

RASTER_WIDE requires PVR+HIRES, native640/scissor height, prior-stride flush,
2304KiB main scratch and two max1MiB RTT buffers. Native640 general batches import/
upload/readback affected regions; aligned row uploads before scene_finish;
software/texture barriers flush. Zero-alpha fills flush regional batches before
checking alpha elsewhere.320 transfer behavior unchanged. Expanded native fast
fixture8GPU scenes PASS0mismatches: RGB/alpha outside region, blended backdrop,
odd edges, reset, transparent fills,640x480 edges,320→640→320 transitions.
Full strict SH4 replay vs genuine SH4 software reference PASS. Full fast STILL
four known failures: combined cold/hot, precision30, spans255. Never relax checker.

Rental mode 6 history avg/min: baseline1.8210/1.7676; native VI2.2074/2.1294;
field cache2.2116/2.1333; wide full transfers2.3957/2.2904; packed VI+regional
3.7974/3.6403. Mode 7 removes8/9-draw window variation. Fill-span experiment showed
exactly no timing gain and was REMOVED. Short mode 6 profile ELF built, never run.
Old balanced/limited prototypes are not final deliverable. See performance doc.

No active task-owned Flycast remains. Use isolated validation/flycast-2.6 only;
verify executable before taskkill. Hidden checkpoint startup can take minutes.
Scrape console separately from native Python/Docker (detachment invalidates handles).
SDK bash escalation, DREAMSDK_HOME=C:/DreamSDK, no parallel SDK starts (shared
config lock). Docker arm32 explicit CC/qemu. PythonC:/Python314, Pillow; PPM→PNG
before view_image. No AGENTS; computer-use skill read earlier, no UI actions needed.
Use core.whitespace=cr-at-eol for diff check. Preserve original handoff suffix below.

Native `make -f Makefile.dc HOST=1` is rejected at Makefile parse time on LP64
(`sizeof(long) != 4`). Use `make -f Makefile.dc host-test` (Docker armhf + QEMU).
`make -f Makefile.dc` without `KOS_BASE` points at that host path. `GFX=soft`
adds `test-soft` to `test`. `GAME_DISC=1` requires `GFX=soft`. `VIDEO=` / `DEMO=`
are KallistiOS-only.

## Latest: Stadium 2 gameplay — 2026-09-24

Fast mode now shares strict's qualified opaque one-cycle fills. Exact batches
skip framebuffer import and use sparse masked readback; transitions to/from
general texture batches flush explicitly. Fast passes the previously failing
fill-preservation, color-ramp and software/GPU transition fixtures. New
fill/texture/transparent-fill coverage passes too. Strict and the full highres
host suite pass. General fast texture/state tests still fail intentionally.
See `tools/dc/VIDEO_COMPAT_FRAMES.md` for evidence and frame-cost measurements.

2026-09-22: binary-alpha fast textures now use ARGB1555; fractional combined
alpha stays ARGB4444, opaque stays RGB565. Expanded synthetic replay measures
channel error 124 -> 74, maximum 2 -> 1; transparent/partial-alpha probes pass.
Exact fast texture mismatches remain 30/66 (was 42), so keep its negative-control
status and strict software fallback. See `tools/dc/STADIUM2.md` for evidence and
next rounding investigation. Both local fast images were refreshed.

Fast SH4/Flycast reaches the battle command menu via genuine checkpoints
and executes Arcanine's Flame Wheel after its own recorded C-up input.
See [Stadium 2 notes](tools/dc/STADIUM2.md) for build flags, validation logs,
controls and limitations. Host traversed the menus and executes an attack.
New optional `VIDEO_HIRES=1` supports its interlaced menus; E5 rectangles and
two-cycle untextured depth triangles are implemented. The battle transition
fault was a fill/scissor off-by-one overwriting the next framebuffer descriptor,
not a CPU/TLB failure. Temporary guest-address tracing was removed.

Use isolated `build/dc/validation/flycast-2.6/flycast.exe` and private images in
`build/dc/`; do not touch the user's other emulator/project instances. The fast
menu milestone is not strict accuracy, 5 FPS, physical hardware validation or
an uninterrupted cold-boot-to-battle run. Default + high-resolution full ARM32
regression suites pass. Downloads Zelda launcher remains unchanged.

## Integration checkpoint — 2026-09-20

2026-09-21 controlled replay follow-up: `SOFTWARE_ONLY=1 REFERENCE=1` now
provides the genuine SH4 software baseline. Strict passes against it; fast fails
six groups as expected. The checker requires backend identity and rejects GPU
or cross-CPU references. Pokémon Stadium 2 host trials reached display lists
but zero presented frames: 640-wide/interlaced VI and unsupported two-cycle
untextured depth triangles are concrete blockers. See
[results and next bounded work](tools/dc/DUAL_BUILD_RESULTS.md). No game assets
are tracked; Downloads launcher remains unchanged.

Subsequent planning audit at `50c839b`: see
[dual-build learning plan](tools/dc/DUAL_BUILD_LEARNING_PLAN.md). Next priority is
same-revision strict/fast comparison, a genuine software-only SH4 replay, and
first-divergence plus capability-cost accounting. Existing strict texture tests
can pass through fallback; `REFERENCE=1` does not disable KOS/PVR. Qualify useful
GPU subsets individually; keep cache regression work as a bounded supporting task.
This audit changed documentation only and launched no new benchmark.

2026-09-21 sampler follow-up: strict menu improves **0.516 -> 0.586 FPS**
(13.6% throughput), frames 60-120 / 102.413547 s in `floor-game.log`. The final
private image is `not64-oot-quality-floor.cdi`; Downloads remains unchanged.
An 8 KiB decoded-sample cache invalidates on TMEM, palette, LUT and tile/size
changes; filter arithmetic is unchanged. Positive bounded coordinate floor uses
exact truncation. `SOFT_PROFILE=1` adds per-draw-category timing; the ARM menu
profile found textured triangles account for ~86% of raster time. Keep profiling
off for FPS results. The cache-only warm result was 0.561 FPS.

All host regressions pass. Menu/opening images and the menu's complete saved
state match their references. Replay now includes 64 sample mutation/boundary
cases in addition to the prior 96 pixel/state cases. **Use same-target references**:
the unchanged SH4 and ARM three-point filters differ near floating-point
boundaries. `floor-kos.log` matches `floor-kos-reference.log`; `floor-host.log`
matches `floor-reference.log`. No comparison tolerance was relaxed. Evidence is
in ignored `build/dc/validation/`, including `floor-scenes/` and
`triangle-profile/`. Optional profiler also compiles on KOS.

Known tradeoff: nearest/clamped full-screen rectangle microbenchmark slows
4.73 -> 5.31 s with the cache. Raw out-of-bounds coordinates miss despite
clamping to the same texel. Next: test cache admission/canonicalization or exact
triangle/blender specialization against both this case and the menu. Compatible
5 FPS and the full Q5 acceptance criteria remain unfinished.

2026-09-21 exact-span follow-up: opaque one-cycle software texture rectangles
hoist invariant blend/bounds checks while preserving sampler, combiner and final
blender state. `REFERENCE=1` on `tests/dc/Makefile.raster-replay` selects the old
loop in a separate output directory. Use `check_raster_replay.py --reference`
for 96 framebuffer/depth/boundary/raw-state comparisons. Full ARM32 tests pass;
the 60-frame menu image and complete saved state match `direct-opt` byte for byte.
The target synthetic workload improves 8.40 -> 4.73 s, but strict menu frames
60-120 still take 116.322003 s (0.516 FPS): **no measurable menu gain**.
Evidence: `span-*.log` and `span-menu/` in ignored validation. The private test
image is `not64-oot-quality-span.cdi`; Downloads is unchanged. Next: profile and
qualify textured-triangle spans and identify this menu's remaining rendering cost.

2026-09-21 correctness follow-up: the initial Q0/Q1 slice now has a ROM-free
RDP replay on ARM32 and real KOS/PVR readback. Opt-in `RASTER_STRICT=1` passes
exact disjoint-fill RGB/alpha, opaque color ramps, mixed CPU/GPU writes and
software COMBINED-state checks. GPU scene counts verify actual acceleration.
The experimental mode fails the negative control. Strict currently accelerates
only opaque one-cycle fills; textures and blends remain software. It is not a
new 5 FPS claim, and the Downloads launcher remains on the experimental build.
Strict menu frames 60-120 take 116.317821 s (0.516 FPS); see
`quality-strict-game.log`. The next step is qualified texture acceleration.
See [quality plan](tools/dc/FPS_QUALITY_PLAN.md) and
[replay instructions](tools/dc/RASTER_REPLAY.md). Local evidence is
`quality-replay-{host,strict,fast}.log` and `quality-full-host.log` in validation.

2026-09-21 current launcher: **OOT file selection exceeds 5 actual FPS in Flycast.**
The Downloads launcher uses `not64-oot-5fps.cdi` and a separate official
`flycast-not64-2.6` installation (OpenGL, RTT readback on, framebuffer emulation
off). Its previous software renderer remains available through
`Play Zelda OOT (software fallback).cmd` and `not64-oot-fast3.cdi`.
The opt-in `RASTER_PVR=1 INTERP_DIRECT=1` build renders the native-resolution
menu using cached PVR geometry/textures, with synchronous RDRAM fallback for
unsupported operations. Paired VI conversion and checkpoint CRC are also faster.
`fps5-pvr14-target.log`: frames 60 -> 420, total-us 14006507 -> 80384361:
360 frames / 66.377854 s = **5.423 FPS**, with 32006 Hz AICA active.
The full ARM32 default and direct-interpreter suites pass; the final 60-frame
software menu capture and complete saved state match the reference byte for
byte (`fps5-default-host.log`, `fps5-final-host.log`, `direct-opt/final.log`).
All evidence lives under ignored `build/dc/validation/`. See
[experimental PVR backend](tools/dc/PVR_RASTER.md) for build flags and limits.
This validates the file-selection checkpoint, not full gameplay or real hardware.
PVR filtering/alpha differ from the reference; existing menu seams remain.

2026-09-21 previous: Downloads launches `not64-oot-fast3.cdi`. Direct S/T
translation avoids temporary arrays, and the combiner caches zero-product
RGB equations while retaining alpha and combined state. Flycast menu time
is 121.702 s per 60 frames vs 126.680 s (4.1% throughput gain; 30.0% over
the original baseline). Host menu/opening captures are byte-identical and
the full suite passes with coordinate/mux regression coverage. Evidence is
`perf3-*.log`, especially `perf3-target.log` and `perf3-regression.log`.
AICA starts; the menu still runs at only ~0.49 FPS. No full gameplay claim.

2026-09-21 follow-up: Downloads now launches `not64-oot-fast2.cdi`. Three-point
filtering omits the unused fourth texel and blender modes skip unused memory
RGB reads. Flycast's same 60-frame menu sample improves 141.779 -> 126.680 s
(another 11.9% throughput gain, 24.9% over the original baseline). Menu/opening
host captures remain byte-identical; full suite and focused alpha/blender/filter
tests pass. `perf2-target.log` and `perf2-*.log` contain evidence. The running
menu is visually verified with AICA active. Speed is still only ~0.47 FPS.

Performance follow-up: Downloads now launches `not64-oot-fast.cdi`, the menu
checkpoint build with `PERF=1 LTO=1`. The same first 60 menu frames measure
158.247 s before vs 141.779 s after in Flycast (11.6% throughput gain).
Renderer changes skip unused depth/blend work, cache tile dimensions and avoid
unneeded texture samples. Menu and opening-scene host captures are byte-identical;
full optimized host suite passes. See `VIDEO_AUDIO_PERF_PLAN.md` and local
`perf-*.log` evidence. The menu is still far below playable speed (~0.42 FPS).

Latest milestone: **OOT file selection is verified in Flycast**, showing
File 1–3 / Copy / Erase / Options. The Downloads launcher now opens
`build/dc/not64-oot-menu.cdi`, built with `GAME_CHECKPOINT=1` and a private
`/cd/boot.st` captured after reaching the menu through two ordinary Start pulses.
Host evidence: `oot-menu2.log`/`.png` (600 resumed frames, no decode failures).
Target evidence: `oot-menu-flycast.log`, loaded checkpoint and 32006 Hz AICA,
plus visual verification in the Flycast window. Full host regression passed
in `oot-menu-regression.log`. No new save file/name was created.
The shortcut always restores this menu snapshot, not subsequent gameplay.
Use `not64-oot-polished.cdi` for normal boot without restoring a snapshot.
Host CLI now supports repeated `--start-at`, `--save-slot`, and `--load-slot`;
the checkpoint files contain private ROM-derived data and remain ignored.

Zelda OOT follow-up: the local USA ROM reaches the opening scene. The host
completed 600 frames / 334 lists with no decoder or presentation failures;
Flycast now boots the game and starts 32006 Hz AICA output. Fixed missing
two-cycle texture rectangles, F3DEX2 CULLDL, and a DreamSDK C++ startup crash
caused by libgcc resolving `mutex_lock` to a weak no-op. The repo supplies
the real KOS wrapper in `platform/dc_kos_compat.c`; no SDK edits are needed.
`GAME_FRAMES=0 GAME_STEPS=0 GAME_START=0` makes a game-disc build interactive.
The updated local unrestricted image is `build/dc/not64-oot-polished.cdi`,
launched by the Downloads shortcut. Copy-mode rectangle clipping is also fixed
and regression-tested in both axes. `oot-progress.log`/`.png` in validation
record 1,500 presented frames / 634 lists and the title screen, with no decoder
or presentation failures. That run used Start at VI 700 (too early to establish
menu entry); next try a later press. Title textures remain visibly corrupted.
Rendering and
speed are imperfect; full gameplay and hardware compatibility are unverified.

Audit follow-up: `oot-audit-suite.log` records the full host suite passing,
including expanded CULLDL shared-plane/spanning/invalid-range coverage.
`oot-menu-audit.log`/`.png` records 1,800 frames / 734 lists with zero renderer
failures, but Start at VI 1450 still left the final capture at the title screen.
Do not claim file selection or gameplay is verified; investigate input delivery
and title timing next. The console capture helper now validates read/resize
results, requires an output path for reads, and closes its console handle;
`oot-audit-flycast.log` verifies capture from the running Downloads instance.

V2/A1 follow-up: KOS stereo `snd_stream` output is wired through a 5 ms worker,
with ring conversion, silence padding, rate/mute/pause and joined teardown.
Host PCM tests and Flycast's three-cycle audio diagnostic pass. V2 is available
as `UPLOAD_SKIP=1`, but stays off by default: Flycast XXH32 costs ~3.7 ms/frame
versus ~30 us for V1 DMA despite reducing static uploads from 120 to one.
See `VIDEO_AUDIO_PERF_PLAN.md` and `tools/dc/README.md` for evidence and commands.
Hardware listening/timing and game audio quality remain unverified.

Resumed at the user's request to finish, merge, commit, and push all work.
The graphics changes and the performance/menu checkout are preserved in commits
and combined on `master`. The historical checkpoints below describe earlier
states; use `tools/dc/README.md` for current build commands and limitations.
Native LP64 builds are intentionally rejected. Both software and PVR target
builds include the ROM browser; diagnostic presets bypass the browser.

The merged host suite passes CPU, controller/Joybus, menu, TLB cache, memory
contract, VI conversion, exact image capture, and all three ROM dump formats.
The optimized SDK-endian regression and software renderer checks also pass.
A generated large fixture exercises ROM-cache replacement and file reuse.
Diagnostic builds release graphics/CPU/cache resources and idle safely until
the emulator is closed. ReIOS reboots a mounted disc when asked for the BIOS
menu, so a menu exit alone did not prevent repeated startup. The log validator
rejects fatal errors and accidental restarts and requires successful cleanup.
Final Flycast diagnostic-disc validation passes: 272 frames, eight reopen
cycles, stable PVR allocations, successful cleanup/idle, and no restart.
Evidence: build/dc/validation/merged-disc-idle.log. The final game-disc ELF
builds; the full commercial-game run was not repeated after integration.
Physical hardware and full gameplay remain unverified.

## Historical stopping checkpoint — 2026-09-20

Paused at the user's request. All source changes remain on disk, uncommitted;
no commit or push was made. No task-owned emulator or host test is still running.
The user's separate Flycast instance and existing SDK Docker container were left
untouched.

Verified: Mario Golf opening/title display in Flycast through PVR, 600 presented
frames / 302 display lists, zero reported presentation or decoder errors during
that bounded run. Host regressions and focused software-renderer UBSan checks pass.
Local test disc: `build/dc/not64-game.cdi`; reproducible build instructions and
limitations are in `tools/dc/README.md`.

Important follow-up found while stopping: the complete Flycast serial log shows
another KOS startup after the successful run and display-mode restoration, then
`Fatal: SH4 exception when blocked`. Its cause has not been investigated. The
600-frame display result is valid, but clean game-disc exit/restart is NOT verified.
Complete evidence is saved in `build/dc/validation/mario-golf-flycast.log`.
Investigate that exit/restart failure before broadening gameplay or performance work.
Physical Dreamcast hardware and full gameplay remain unverified.

## Current implementation update — 2026-09-19

The software VI display and native PVR textured-quad presenter now both render
CPU-written color bars in Flycast: 120 valid frames, all 76,800 source pixels
checked, zero reported presentation failures. Host memory/lifecycle, converter,
CPU/PIF/AI/save and exact image-capture tests pass. Target testing also fixed a
KOS byte-order constant that incorrectly selected big-endian CPU/RSP paths.

Use [tools/dc/README.md](tools/dc/README.md) for reproducible Docker builds,
self-contained software/PVR demo ELFs, SDK provenance, and test scope. Native
LP64 host builds remain intentionally rejected. Both presenters passed eight
Flycast reopen cycles and invalid/blank recovery, with stable PVR allocations and
recorded emulated-clock timings. Physical Dreamcast hardware remains unverified.
Opt-in `GFX=soft` now renders F3DEX2 game tasks into RDRAM for VI/PVR display.
Mario Golf (USA) displays its title screen in both the host capture and Flycast
through PVR after a deterministic Start pulse. Corrected ROM byte order, reset VI timing, and Joybus button packets
allow the boot and input transition. See the game-disc instructions and precise
limitations in [tools/dc/README.md](tools/dc/README.md). Raw DPC, broad microcode
compatibility, accurate coverage/VI filtering, and full gameplay remain unverified.

The sections below preserve the earlier handoff/plan; this update supersedes
older statements about missing ELFs, stub graphics, and untested emulator boot.


**Read this first, then `PORTING.md`.** This file is for the next agent, not a design essay.

| | |
|--|--|
| Repo | `awest813/Not64---Dreamcast-port` (tree is Wii/GC Not64) |
| Branch | `cursor/dreamcast-port-plan-fc3a` |
| Base | `master` |
| PR | https://github.com/awest813/Not64---Dreamcast-port/pull/1 (draft) |
| HEAD at handoff | After the TLB/ROM-cache hot-path work and the Phase 8a ROM browser (see git log) |
| License | GPL v2 |
| Cloud env | Host stub builds with gcc **or** clang, Linux and macOS; `Makefile.dc` defaults to `gcc` because an older clang ICE'd on `r4300.c`. Override with `CC=clang`. |
| KOS toolchain | **Docker.** `einsteinx2/dcdev-kos-toolchain:latest` has sh-elf-gcc 9.3.0 + KOS 2.x and ships a native arm64 image. `not64-dc.elf` builds and boots. |

---

## What to run before touching anything

```sh
make -f Makefile.dc HOST=1 test
```

Must print **`CPUTEST PASS`** and, per process, **`map smoke PASS`**,
**`menu smoke PASS`**, **`tlb cache smoke PASS`** and **`pif smoke PASS`**,
then exit 0. Second process is `roms/dc_dummy.z64` (IPL spin at `0xa4000040`).

`rom cache smoke` is **host stub only**, and reports **SKIP** on the 4 KiB
bring-up images — it needs a ROM larger than the 1 MiB stream window. Drop a
real dump in `./roms` to run it for real; it then reports the sweep, the
page-ins and the file opens. It stays off the KOS build on purpose: the sweep
reads the whole cart, which is free from a host filesystem and would mean
pulling 32 MiB off the card before every boot on hardware. `smoke_tlbcache()`
does run on KOS — it is a few thousand operations, and it is worth checking
the structure where `unsigned long` is 32-bit.

### Dreamcast build (the real one)

```sh
docker run --rm -v "$PWD":/src -w /src --user "$(id -u):$(id -g)" \
    einsteinx2/dcdev-kos-toolchain:latest \
    bash -lc 'source /opt/toolchains/dc/kos/environ.sh && make -f Makefile.dc'
```

That produces `not64-dc.elf`. **Do not hand the .elf to an emulator** -- the
KOS framebuffer examples come up black that way too, so it is the loader, not
us. Build a disc instead, which is how it would ship anyway:

```sh
docker run --rm -v "$PWD":/src -v /tmp/out:/out -w /src --user "$(id -u):$(id -g)" \
    einsteinx2/dcdev-kos-toolchain:latest \
    bash -lc 'source /opt/toolchains/dc/kos/environ.sh && \
              platform/dc_menu/mkdisc.sh /out/not64.cdi roms/*.z64'
```

Everything after the output path lands on the disc as `/cd/roms`, which is the
first place the browser looks. Flycast boots the `.cdi` with its HLE BIOS; set
`Dynarec.Enabled = no` in `emu.cfg` if its SH4 driver asserts on startup (the
check is memory-layout dependent and fails on roughly two runs in three).

```sh
make -f Makefile.dc HOST=1
./not64-dc-bringup                          # default: roms/dc_cputest.z64
./not64-dc-bringup --menu                   # run the browser, print the screen
./not64-dc-bringup roms/dc_dummy.z64
./not64-dc-bringup /path/to/game.z64 50000000   # argv[2] = step budget
```

`argv[2]` is the interpreter step budget (0 = run until the ROM stops). The
bring-up prints **steps actually retired** and a post-run state dump (COP0,
MI, VI, SP/DPC) ending in a VERDICT line. `VI origin` is the Phase 4 gate:
non-zero means a ROM handed over a framebuffer.

Do not commit game dumps. `.gitignore` excludes `/roms/*.z64|n64|v64` except
the two generated bring-up images.

---

## Where the work actually is

The Wii/GC product still builds from `Makefile.menu2_*`. Dreamcast is a **third platform**: interpreter core + new I/O, **not** a PPC dynarec or GX port.

| Path | Role |
|------|------|
| `PORTING.md` | Plan, phase checklist, memory budget |
| `Makefile.dc` | `HOST=1` → `not64-dc-bringup`; else KOS `not64-dc.elf` |
| `main/main_dc.c` | Bring-up: budget, ROM dir, Maple, saves, map/IO/PIF smokes, load, 10000 interp steps, CPUTEST |
| `main/rom_dc.c` | Header + **LE z64 word swap** (`BYTE_SWAP_HALF` for magic `0x80371240`) |
| `main/ROM-Cache-dc.c` | 1 MiB stream window |
| `platform/dc_*.c`, `dc_types.h`, `dc_memory.h` | Types, budget, plugins/savestate/timer stubs, `prefetch_opcode` |
| `fileBrowser/fileBrowser-kos.c` | POSIX `./roms` `./saves`; KOS `/sd/not64/...` |
| `gc_input/controller-DC.c` | Maple `controller_t`; trigger/shift map (see below); host inject `controller_DC_host_set*` |
| `platform/dc_menu/` | Phase 8a ROM browser. `dc_draw.h` surface; `dc_draw_kos.c` (bfont, **never compiled**) / `dc_draw_host.c` (character grid, for tests); `dc_menu.c` logic |
| `gc_audio/audio-dc.c` | AI DMA → ring; no AICA yet |
| `roms/gen_dc_roms.py` | Regenerates dummy + CPUTEST `.z64` |
| `roms/dc_cputest.z64` | IPL: ALU + SW/LW + **SRL/SRLV/SRA/SRAV/SLLV/SUBU/DIVU/MFLO/MFHI on a negative operand** + a 64-bit sign-extension check + BEQ spin; header carries `'DO'` / `'E'` / v1 for the DC header un-swap |
| `roms/dc_dummy.z64` | IPL: `B -1` |
| (your own dump) | `./not64-dc-bringup game.z64 50000000` — see Phase 3.5 in `PORTING.md` |

Core linked on host: `r4300/pure_interp.c` + `gc_memory/` + `rsp_hle/` with `-D__DREAMCAST__ -DNOASM -DUSE_TLB_CACHE`, **without** `-DPPC_DYNAREC`.

---

## Done (do not re-do)

1. **Phase 0 decisions** — interpreter first, no Expansion Pak, no menu, software gfx later, `/sd/not64/` paths. See table in `PORTING.md`.
2. **Phase 1 host bring-up** — console diagnostics, file browser, Maple probe, audio ring stub.
3. **Phase 2 host interpreter** — dummy ROM load + 10000 steps.
4. **CPUTEST** — GPRs including r10=`0x1334` r11=`1` + `rdram[0]==0x1333`, PC in `{0xa4000074, 0xa4000078}`.
5. **Host I/O smokes** — `./saves/dc_host.txt`, injected A + analog, 64-byte AI DMA into the ring.
6. **PIF/Maple host** — `native_ReadController` → `internal_ReadController`; `update_pif_write` status `0x05`; `update_pif_read` A + analog 72/48.
7. **Audit polish** — DC-only KSEG1/`n64_addr`; Wii `fast_mem_access` **unchanged**; ROM cache clipped; teardown after run; no x86 assembler in DC `recomp.h`.
8. **Interpreter LP64 fixes** — `macros.h` + `pure_interp.c` 32-bit ops; a real ROM now boots past IPL3. CPUTEST covers all of it (each fix fails the test when reverted).
9. **Controller map** — triggers carry Z/R, `X` or `Y`+left trigger is L, both triggers shift the D-pad to the C-buttons. See **Controller mapping** below.
10. **Second audit polish** — host stub builds under clang/macOS (`gc_input/input.c` nested functions removed, `<malloc.h>` guarded, `invalidate_func` declared); DC ROM-header byte order fixed and asserted; PIF joybus store made alignment- and LP64-safe; ROM cache bounds/LRU/NULL fixes; AI DMA clamped to RDRAM; `PC` no longer leaked per run; recursive `mkdir` for `/sd/not64/...`; `get_savespath()` correct on KOS.
11. **Hot-path performance.** Three places did linear work per access; none of it shows on a 3 GHz host, all of it matters on a 200 MHz SH4 with 16 MB.
    - `TLB-Cache-hash.c` hashed the page by its **top** bits, so 16,384 consecutive pages shared a bucket; storing 0 left a tombstone node forever (heap heading for ~16 MB). Now keyed on mixed low bits over 1024 slots, stores of 0 unlink, re-maps update in place, freed nodes recycle. Lookup 260.2 ns -> 1.5 ns, full re-map 633.3 us -> 2.65 us, both versions returning an identical checksum over 20M lookups.
    - `ROMCache_read`/`_write` bumped an age counter for all 1024 blocks **per call** — 4 KiB of writes for a four-byte cart read. Now an O(1) monotonic clock.
    - `fileBrowser_kos_readFile` did `fopen`/`fseek`/`fclose` per call, one per 64 KiB page-in. Read handle now held open: sweeping a 32 MiB ROM went 514 opens -> 2 for the same 496 page-ins. Writes still open and close, so saves are unchanged.
    `smoke_tlbcache()` (host and KOS) and `smoke_romcache()` (host only — see above) cover both; each was verified to fail when the behaviour is reverted. Note `ROMCache_deinit()` now closes the cached handle: holding it open across calls means something has to let go of it.
12. **Phase 8a ROM browser** — `platform/dc_menu/`. Lists the ROM dir, pad picks, boots. Optional and never load-bearing: `skipMenu` is 1 on the host, an explicit ROM argument forces it, and `make ... test` never enters it. `smoke_menu()` covers the filter, sort, edge detection, auto-repeat, wrap, scroll window, paging, pick/cancel, empty list and `dc_menu_run()` end to end. `./not64-dc-bringup --menu` prints the screen it drew. See Phase 8 in `PORTING.md`.
13. **Controls + session log** — X is L (including during C-shift); Y+LT is L without Z. Overlay and `saves/not64.log` are independent (`dc_log()`). Host poll is port 0 only.
14. **VI/present kernel polish (performance audit).** `dc_vi_convert` re-cleared
    the whole 256 KiB texture and did a byte-split halfword read per pixel on
    every VI interrupt; `dc_video_expand_2x` did four scalar stores per pixel.
    Now the 16-bit converter takes two pixels per aligned word load (odd-row
    starts keep the per-pixel path; an odd-width tail reads the word's high
    half), clears only the padding the row loop will not overwrite, and the 2x
    presenter writes one aligned 32-bit pair per pixel plus one row copy per
    scanline pair. Also audited and deliberately left alone: the interpreter
    loop and `prefetch_opcode` (locked pure-interpreter design), the soft-gfx
    rasterizer (upstream per-span math, no test harness for pixel regressions),
    DMA/PIF/audio (event-driven, not per-instruction), and the ROM-cache
    eviction scan (runs next to a 64 KiB card read). Byte-exactness evidence:
    `test_vi.c` gained an odd-width aligned-row vector and an aligned fast-path
    expand vector, the exact-capture SHA-256 is unchanged, and a 200k-state
    differential fuzz of the new converter against the pre-polish version
    (scratch harness, not committed) reported identical textures on ~80k READY
    frames per seed.
    Follow-up: `dma_pi_read`/`dma_pi_write` also swept every byte of every cart
    DMA calling the no-op `invalidate_func` stub (the sweep exists for the
    Wii/GC recompilers), so Dreamcast now skips it — that loop ran per game
    DMA during gameplay, not just at boot. Benchmarked on the ILP32 regression
    build under QEMU, the polished converter converts a 320x240 16bpp frame
    1.85x faster at the target's `-O2` (90.9 vs 167.8 us/frame; the ratio, not
    the absolute time, is what transfers — SH4 cycle timing still needs the
    GCC 15.1 SDK build plus Flycast, and the toolchain here is single-lib
    `-m4-single-only`, so KOS cannot just be rebuilt for the 64-bit-double
    contract). The differential fuzz passes at `-O2` as well.

---

## Locked (until a human changes them)

- Pure interpreter only. Do **not** compile `r4300/ppc/` or enable `PPC_DYNAREC`.
- No 8 MiB TLB LUT — `USE_TLB_CACHE` hash (`TLB-Cache-hash.c`).
- No 4 MiB `blocks[]` on DC (`r4300.h` / `r4300.c`).
- No Expansion Pak (`USE_EXPANSION` unset) — 4 MB RDRAM.
- No `libgui/` / GX menu.
- First frame = software renderer later (`GX_gfx/` / `mupen64_soft_gfx/`), not PVR and not KGL-from-desktop-GL.
- `#ifdef` is not a HAL; new DC code goes under `platform/` and `Makefile.dc`.

---

## Landmines (we already hit these)

| Symptom | Cause | Fix already in tree |
|---------|--------|---------------------|
| Clang ICE on `r4300.c` (older clang only) | Compiler bug | `Makefile.dc` defaults `CC = gcc` when `HOST=1`, but only overrides make's built-in default, so `CC=clang` works |
| Immediate segfault at start | Custom `.sbss` on host ELF | `N64_SBSS` empty on `__DREAMCAST__` |
| Crash in `SLL` / bad `rd` pointer | `prefetch_opcode` wrote every union format | One format per opcode in `platform/dc_recomp_stubs.c` |
| Garbled ROM name then wrong opcodes | LE `*(uint32*)` magic; z64 bytes left BE | Byte-wise magic; `BYTE_SWAP_HALF` for z64 on DC |
| Segfault on `SW` to `0x80000000` on x86_64 | `address` is 64-bit; `0x80000000` sign-extends; `rwmem[address>>16]` explodes | `n64_addr()` in `gc_memory/memory.h` (**DC only**) |
| Boot executes RDRAM not IPL | Wii `fast_mem_access` treats only `0x8/0x9` as unmapped; `0xa4000040` TLB-misses | KSEG0+KSEG1 unmapped **on DC only** — do not change the Wii `#else` |
| `src` undeclared in stubs | Copied `prefetch_opcode` from `recomp.c` (`src`/`dst` live there) | Stubs decode locally; no `src`/`dst` |
| Host `.o` stale after a header change | The hand-written dep list omitted `macros.h`; edits silently reused stale objects and produced runs that disagreed with the source | `Makefile.dc` uses `-MMD -MP` and `-include $(DEPS)`. **If a result looks impossible, `make -f Makefile.dc HOST=1 clean` first.** |
| Real ROM dead-loops at `0x800001c8` (`BGEZAL r0,-1`) | IPL3's CIC checksum failed. `long` is 32-bit on SH4/PPC but 64-bit on an LP64 host, so `sign_extended()` stopped truncating and `SRL`/`SRLV`/`DIVU` shifted a sign-extended value | `int`/`unsigned int` in `r4300/macros.h` and `r4300/pure_interp.c`. Identical codegen on GC/Wii. See Phase 3.5 |
| A 32-bit op looks right but breaks a real ROM | Only the **low** 32 bits were checked; the high half held garbage, and IPL3 compares 64-bit registers with `SLTU` | CPUTEST checks `r22` as a full 64-bit value, and exercises shifts on a **negative** operand |
| `error: function definition is not allowed here` in `input.c` | GCC nested functions in `load_configurations` | Replaced with `read_config_pointer()` + local macros; call sites unchanged |
| `malloc.h: file not found` on macOS | Darwin/BSD have no `<malloc.h>` | Guarded in `r4300/r4300.c` and `gc_memory/dma.c` |
| `call to undeclared function 'invalidate_func'` | Only declared by `r4300/ppc/Wrappers.h` | Declared in `dma.c` under `__DREAMCAST__` |
| Garbage `Cartridge_ID` / `Country_code` / region | ROM words stored byte-reversed, so the header's byte/halfword fields scramble while its 32-bit fields stay correct | `dc_fix_header_byte_order()` in `rom_dc.c`; CPUTEST asserts `'DO'`/`0x45`/`isEEPROM16k` |
| CPUTEST "passes" after a loader regression | Checks were gated on `goodname == "DC CPUTEST"`, which a broken loader never produces | `check_cputest(path)` fails when the CPUTEST image was requested but did not decode |
| Memory dumps read as zeros while the interpreter clearly executes that memory | `rdram` is declared `unsigned long rdram[SIZE/4]`, and `unsigned long` is **8 bytes** on an LP64 host, so `rdram[addr>>2]` walks at double stride. The emulator does not use it that way: `read_rdram()` is `*(unsigned long*)(rdramb + (address & MEMMASK))`, a **byte** offset | Inspect N64 memory as `*(unsigned int *)(rdramb + (addr & MEMMASK))`. Getting this wrong produced three confident, wrong conclusions in one sitting ("RDRAM is all zeros", "the thread pointer is zero") before a dump of known-good code came back zero too |
| A behaviour change "has no effect", or a reverted change still fails | `cp`/edit within the same second as the last build leaves the `.o` newer than the source, so make skips it. This is the stale-object landmine again, in a faster form | `rm -f <the>.o` (or `make clean`) between A/B runs. Any A/B result where the *restored* version also fails is this, not the test |
| (hardware, not yet hit) unaligned 32-bit store | `pif.c` wrote `*(unsigned long*)(Command+3)`; **SH4 faults on unaligned access** and `DWORD` is 8 bytes on an LP64 host | DC branch does a 4-byte `memcpy` |

Other constraints:

- Interpreter macros in `r4300/macros.h` dereference `PC->f.*`. `PC` must be `malloc`’d (`pure_interpreter`) and `prefetch_opcode` must fill the **right** union arm.
- `go()` with `dynacore == 2` → `pure_interpreter()`. `cpu_init()` sets `interpcore = 0` then DC forces `dynacore = 2` in `main_dc.c`.
- `init_memory()` zeros SP DMEM; `cpu_init()` copies IPL from ROM `0x40` into `SP_DMEM+0x40`. Order in `main_dc.c` is rom_read → init_memory → plugins → `cpu_init` → `go()`.
- Dummy/CPUTEST IPL runs from **`0xa4000040`**, not `ROM_HEADER.PC` (dummy PC is 0).
- `BYTE_SWAP_HALF` in this tree is a **32-bit endian swap**, not 16-bit.
- Do not grow `#ifdef __DREAMCAST__` through `glN64_GX/` until Phase 4.
- Do not “fix” Wii MEM2 / `blocks[]` / PPC paths as part of DC work.

---

## Controller mapping

A retail Dreamcast pad has no C, Z or D buttons and no second D-pad, so the
N64's Z, L, R and C-buttons ride on the analog triggers and two shifts:

| Dreamcast | N64 |
|-----------|-----|
| A / B / Start | A / B / Start |
| Analog stick | Analog stick |
| D-pad | D-pad |
| **Left trigger** | **Z** |
| **Right trigger** | **R** |
| **X**, or **Y + left trigger** | **L** (X keeps Z on LT and still works during C-shift; Y+LT is L without Z) |
| **Both triggers + D-pad** | **C-Up / C-Down / C-Left / C-Right** |
| Y (menu) | Quit |

Stick scaling lives in `scale_axis()`: centred Maple `-128..+127` -> N64
`-80..+80` with a 10-count deadzone (KOS `cont_state_t` is already centred).

Mechanics live in `dc_virtual_buttons()` in `gc_input/controller-DC.c`: the two
analog triggers fold into the Maple button word as virtual bits
(`DC_VB_LTRIG`, `DC_VB_RTRIG`, `DC_VB_LTRIG_ALT`, bits 24-26, clear of every
`CONT_*`), so everything still maps through the ordinary `button_t` table and
the default `controller_config_t`. Press threshold is `DC_TRIG_THRESHOLD`
(48 of 255), so a resting finger does not latch a shift.

While both triggers are held, the triggers' own bindings (Z and R) are
**withheld**, so reaching for a C-button does not also mash Z+R. That is the
one judgement call in the scheme, and the place to revisit if a game wants Z
held during C-presses.

`smoke_map()` in `main/main_dc.c` covers the button branches plus analog cases
(centre, both deadzone edges, all four extremes, last-stick read-back).

---

## Open findings (audited, deliberately not changed)

1. **Rumble and paks.** Jump Pack send (`MAPLE_FUNC_PURUPURU`) and VMU Controller Pak layout still need KOS. HOST now latches rumble, persists `pakMode`, and round-trips Joybus mempak.
2. ~~**`fileBrowser_kos_readFile` does `fopen`/`fseek`/`fclose` per call.**~~ **Fixed** — the read handle is held open (item 11 above). Writes still open and close on purpose, so a save reaches the card the moment it is written.
3. **Host stub is not an SH4 model.** `unsigned long` is 64-bit on an LP64 host and 32-bit on SH4, so `rdram[]`, `reg[]` and every `read_*_in_memory()` differ in width and layout. Concretely: `rdram[addr>>2]` is **not** how to read N64 memory here — see the byte-view landmine above. `CPUTEST PASS` on the host is a link/logic check, not evidence about hardware. The alignment and LP64 bugs found in `pif.c` are exactly the class the host stub cannot catch by itself.

---

## Next work (in order)

Follow **Gap plan** in `PORTING.md`. Short form:

1. **P0 — who writes `0x400` to `0x800c833c`** (Mario Golf `ERET`). Byte-view dumps only. Not TLB. **Blocked here: no commercial ROM in the tree.**
2. **P1 — KOS ELF / `dc_draw_kos.c` bfont** when `KOS_BASE` exists.
3. ~~**P2 — `AiReadLength` + ring drain**~~ — host shipped; AICA/`snd_stream` still open.
4. ~~**P3 — native EEPROM/SRAM/Flash on SD**~~ — host shipped; VMU still a human call.
5. ~~**P4 — per-ROM savestate names + cart blobs**~~ — host shipped (`NOT64ST` v3). Atomic apply still open.
6. ~~**P5 — pakMode + Joybus mempak/rumble latch**~~ — host shipped; Jump Pack send and VMU still need KOS.
7. Menu 8a–8d is done. `dc_draw_kos.c` still uncompiled.
8. Software/TA/dynarec only after P0 produces a VI framebuffer.
9. **Video/audio performance plan** — [VIDEO_AUDIO_PERF_PLAN.md](VIDEO_AUDIO_PERF_PLAN.md)
   audits the per-frame and per-task costs (measured where possible) and
   orders the wins: PVR present unserialization, upload skip, soft-renderer
   span work, AICA `snd_stream` wiring, `rsp_hle` bulk sample paths, and the
   `count_per_op`/`vilimit` settings levers.
   **V1 is done (2026-09-20)**: ping-pong textures + double vertex buffer +
   DMA upload; Flycast-measured upload 618 → 30 us/frame, present path
   ≈632 → ≈45 us, full `check_target_log.py` run passes. **DreamSDK
   (`C:\DreamSDK`) is the working target toolchain on this machine** (GCC
   15.1, `-m4-single`): build via its MSYS2 bash, capture Flycast serial with
   `tools/dc/scrape_console.ps1` (resize to 220 cols early, then read).
   V2/A1 are implemented (see the follow-up above). Next: V3 profiling before
   V4 soft-renderer spans; measure V2 on hardware before enabling it by default.

Skip SH4 dynarec and TA/RDP until 1–3 have a ROM that is more than a BEQ spin.

---

## How to change the test ROMs

```sh
python3 roms/gen_dc_roms.py
# Makefile.dc HOST=1 also regenerates them as a dependency
```

CPUTEST encodings (BE in the file; loader swaps to LE on DC/host):

- `ori r1, r0, 0x1234` → `r1=0x1234`
- `ori r2, r0, 0x00FF` → `r2=0x00FF`
- `addu r3, r1, r2` → `0x1333`
- `xor r4, r1, r2` → `0x12CB`
- `andi r9, r1, 0x00F0` → `0x0030`
- `sll r7, r2, 8` → `0xFF00`
- `lui r5, 0x8000` / `sw r3, 0(r5)` / `lw r6, 0(r5)` → `rdram[0]` and `r6` = `0x1333`
- `addi r10, r3, 1` → `0x1334`
- `slti r11, r10, 0x2000` → `1`
- `bne r1, r1, +1` not taken (delay nop)
- `beq r0, r0, -1` at `0xa4000074`, nop delay at `0xa4000078`

Checks live in `check_cputest()` in `main/main_dc.c`. Keep them if you change the IPL.

---

## Cloud-agent git notes (this PR)

- Stay on `cursor/dreamcast-port-plan-fc3a` or `cursor/<desc>-fc3a` off that branch.
- Commit + push; update the existing draft PR with `ManagePullRequest` (`base_branch=master`). Do not open a second PR for the same branch.
- `.gitignore`: `/not64-dc-bringup`, `*.o`, `/saves/dc_host.txt`.
- Prefer not to regress Wii/GC Makefiles. DC-only behavior stays behind `__DREAMCAST__` / `DC_HOST_STUB`.

---

## Suggested first message for the next agent

> Continue the Not64 Dreamcast port from `AGENT_HANDOFF.md` and the Gap plan in `PORTING.md`. Run `make -f Makefile.dc HOST=1 test` first. P0–P5 host work is done or blocked (P0 no cart, P1 no KOS). Next is P6 after a real VI, leftover P4 atomic apply, or Jump Pack/VMU when `KOS_BASE` exists. Do not start TA/RDP or SH4 dynarec.

Update **this file** and `PORTING.md` current-status when a phase actually finishes.
