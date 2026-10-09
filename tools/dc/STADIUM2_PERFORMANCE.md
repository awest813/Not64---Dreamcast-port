# Stadium 2 performance work — 2026-09-29

The revised **3.8 game-progress VI updates/s with less frameskip** target
passes both tested checkpoint scenes in Flycast 2.6. Warm interval is VI 60–600,
with nine 60-update windows. Fresh drawings remain separate from progress.

| Final steady build | Average progress/s | Minimum window | Fresh drawings/s |
|---|---:|---:|---:|
| Battle, actual unlimited live-control player | 4.284973 | 3.827127 | 0.571330 |
| Rental menu, same core configuration | 3.959935 | 3.919644 | 0.527991 |

Mode 7 repeats skips 2,3,3,3: average2.75 and maximum3, below fixed skip-three.
Each rolling 30-list interval draws 8 lists. Mode 6 balanced2/3 remains available.
`build/dc/Play-Stadium2-Steady.cmd` launches `not64-stadium-steady-gameplay.cdi`.
The player resumes the battle-commands checkpoint, accepts live controls, has
no scripted input/automatic Start, and runs without frame or instruction limits.
Existing images/launchers were preserved. Final build flags, image hashes and
paired logs/JSON summaries are in `build/dc/validation/fps38-steady-*`.

Full ARM/high-resolution regressions and steady cadence/interrupt/settings tests
pass. Rental120 and battle-menu360 saved CPU/event tails match no-skip; packed
VI conversion also preserves the rental image and entire saved state exactly.
Full strict SH4 raster replay passes against genuine SH4 software reference;
the focused native-width fast fixture passes all eight GPU scenes. General fast mode
still has its four known texture/state replay failures. These measurements qualify
the two scenes; physical Dreamcast timing, whole-game coverage and full fast
pixel accuracy remain unverified. Historical experiments follow.

## Measured battle checkpoint

Same private `battle-commands.st`, empty input replay, Flycast 2.6/OpenGL,
`VIDEO_HIRES=1 RASTER_PVR=1 INTERP_DIRECT=1 PERF=1 LTO=1`. Warm interval:
presented frames 60–120, 30 display lists. Detailed profiling is disabled in
the optimized timing builds.

| Build | Seconds | Presented VI updates/s | Display lists/s | Raster ms/VI | Other ms/VI |
|---|---:|---:|---:|---:|---:|
| Previous code, profiling enabled | 55.440264 | 1.0822 | 0.5411 | 699.00 | 208.26 |
| Unused second-texture sampling removed | 45.114585 | 1.3299 | 0.6650 | 526.64 | 208.52 |
| Plus identity blender and unused-tile GPU eligibility | 39.730882 | 1.5102 | 0.7551 | 436.98 | 208.45 |
| Plus scanout padding reuse | 39.451319 | 1.5209 | 0.7604 | 436.48 | 208.34 |
| Opt-in varying-shade GPU equations | 32.574592 | 1.8419 | 0.9210 | 322.01 | 208.20 |
| Plus alpha shortcut (no measured gain) | 32.654437 | 1.8374 | 0.9187 | 323.14 | 208.36 |
| Plus opt-in RDRAM dispatch and fetch | 31.184750 | 1.9240 | 0.9620 | 323.22 | 183.83 |
| Plus experimental translucent shade equation | 30.123848 | 1.9918 | 0.9959 | 305.84 | 183.53 |

The latest no-skip build improves this interval's throughput by about 84.0%. These
are presented VI updates, not proof of independently advancing game simulation
or unique rendered frames. Display-list rate is reported separately, and is not
a universal simulation-tick counter either. This is one battle scene, not an
overall-game or completed-battle qualification. The older unprofiled first-60
benchmark was 1.0874 VI updates/s (`VIDEO_COMPAT_FRAMES.md`).

Padding reuse reduces conversion from 16.69 to about 12.64 ms/VI. The unclassified remainder includes
CPU interpretation, RSP audio, scheduling and other work; it must not all be
labelled CPU time. Even eliminating rasterization would leave about 221 ms/VI.
The 15 FPS budget is 66.67 ms/VI, so CPU/RSP and rendering both need substantial
changes. Copy reduction alone cannot meet the goal.

## Implemented and validated

- Both combiner cycles explicitly track whether TEXEL1 is read. When unused,
  software rectangles and textured triangles omit its per-pixel filtering but
  restore the original next-tile sample at every row's last covered pixel.
  This preserves final TEXEL1/alpha state after scissor, alpha and Z rejection.
- The first blender stage skips the exact `X*0 + X*1` identity, retaining the
  same temporary alpha and blended-color state.
- Experimental GPU textures no longer reject mismatched dimensions/shifts on
  an unused next tile. That descriptor is omitted from the cache key, and its
  texels are not baked. Actual TEXEL1 readers retain their checks. In the battle
  sample the dual-tile rejection count falls from 1,682 to zero at frame 60.
- `SOFT_PROFILE=1` records bounded per-combiner/blender state buckets as well as
  the existing draw categories. Profiling shows two-cycle textured triangles
  dominate the raster cost. The two largest battle modes use varying shade:
  `00127fff/3ffe7e3f` with primitive `d2d2d2ff`, and
  `00457fff/3ffcfe3f` with primitive `80808040`.
- `measure_game_perf.py` validates complete paired progress/timing intervals,
  rejects restarts/failures/nonmonotonic timings, and reports VI and display-list
  throughput separately. `--min-fps 15` fails below the target.

The full ARM32 high-resolution suite passes. The exact 120-frame software battle
image and complete saved CPU/RDRAM/device state match the original baseline.
Added 64 differential cases cover unused versus required TEXEL1 RGB/alpha,
tile wrapping, fractional/nearest filtering, flipped rectangles, both triangle
halves, scissor, depth rejection and a later draw consuming the prior TEXEL1.

The same SH4 strict replay passes against the genuine SH4 software-only reference.
The new unused-tile fixture passes analytically in fast mode with GPU execution
established. Fast retains the four existing failing texture/state groups; the
exact checker remains unchanged in its accuracy expectations.

## In-progress experiments and next work

The direct-call opcode-switch experiment passed ARM32 regressions and exact
battle image/state comparisons but regressed SH4 throughput to 1.4720 VI/s
from 1.5102. Its code and build flag were removed; private evidence is retained.

Scanout padding reuse is implemented and validated. The graphics plugin owns a
padding cache and invalidates it on lifecycle, error and mode changes. The ordinary
converter retains its unconditional padding contract. The cached path avoids
re-clearing 874 KiB of already-black padding for a stable 320x240 frame in a
1024x512 high-resolution allocation. Full staging-buffer comparisons cover size,
format, crop, blank/error and interlaced transitions. SH4 conversion cost fell by
24.3%; throughput rose by 0.7% because rendering dominates.

`RASTER_SHADE=1` is an opt-in GPU experiment for two separable opaque varying-shade
equations. `(TEXEL0-PRIM)*SHADE + SHADE` uses texture modulation and a positive
`(255-PRIM)*SHADE/255` offset. `(SHADE-PRIM)*PRIMALPHA + TEXEL0` uses an additive
offset, restricted to nonnegative vertex differences. Neither reads TEXEL1;
alpha coverage must force opaque output. Strict mode retains software fallback.
The new six-case interior-color probe includes varying depth and a negative
bias fallback. SH4 fast completes four scenes with maximum error two five-bit
channel levels and exact alpha; ARM32 software has zero error. This bounded
probe does not establish edge coverage or exact full-image parity. The full fast
replay still fails its four existing texture/state groups, so the shade path must
remain experimental. The benchmark reduces raster cost from 436.48 to 322.01
ms/VI and increases this battle interval's throughput by 21.1%.

An exact zero-product alpha-combiner shortcut passes ARM32 image/state and SH4
strict/reference replay, but provides no measured timing gain in this battle.
`CPU_PROFILE=1` adds opcode counts (including delay slots), periodic page samples
with approximate Space-Saving weights, and separate RSP task timings. Profiling
changes timing and must remain disabled in performance qualification builds.

The RDRAM dispatch/fetch experiment (`INTERP_MEMORY=1`) passes the full ARM32
suite, exact battle image/state comparison, and installed-handler checks at all
eight bus widths. It inlines only installed ordinary RAM handlers; MMIO, TLB
and framebuffer callbacks retain dispatch. SH4 other time drops from 208.36
to 183.83 ms/VI, improving throughput by 4.7%. Keep it opt-in until broader
gameplay qualifies it.

CPU profiling measures 15,260,538 instructions over frames 60-120, including
delay slots. LW, SW and ADDIU dominate the primary opcode mix; page 8003d000
dominates approximate samples. RSP audio takes 678,714 us over the same 60 VI
updates (11.31 ms/VI). RSP graphics timing includes renderer execution, so
subtracting it again from the renderer total would double-count it.

The additional translucent shade equation is qualified by the same bounded
interior-color probe under the experimental flag. It rejects nonopaque source texels before GPU submission
because filtering cutout alpha and then squaring it is nonlinear. Depth/fog
states need separate qualification. A much faster CPU execution path is also
necessary for the final goal.

## User-authorized frameskip

The user explicitly requested some frameskip. `--frameskip 0..5`, the saved
`frameskip` setting, and game-disc `GAME_FRAMESKIP=0..5` select one drawn display
list followed by N skipped lists. Default remains zero. Skipped lists still
decode state and deliver FullSync; only RDP drawing is suppressed. This is a
list-based policy; games with multiple lists per visual frame need qualification.

The benchmark reports VI progress, drawn/skipped lists, and presentations
following a newly drawn list separately. Repeated presentations do not count
as fresh drawings. `measure_game_perf.py --metric new-renders --min-fps 15`
rejects the current result. None of these runs establishes overall 15 FPS.

Same checkpoint, warm frames 60-300 (120 display lists), skip three:

| Build | Seconds | VI updates/s | New drawing presentations/s | Raster ms/VI | Convert ms/VI | Other ms/VI |
|---|---:|---:|---:|---:|---:|---:|
| Translucent shade + RAM path, skip 3 | 69.152357 | 3.4706 | 0.4338 | 105.33 | 12.67 | 170.08 |
| Plus tiny ALU dispatch + stable scanout reuse | 67.310754 | 3.5656 | 0.4457 | 105.43 | 8.30 | 166.67 |
| Plus common load/store dispatch | 66.282880 | 3.6208 | 0.4526 | 105.45 | 8.26 | 162.41 |
| Plus RAM fetch directly in interpreter loop | 63.109197 | 3.8029 | 0.4754 | 105.41 | 8.26 | 149.22 |

Each draws 30 and skips 90 lists in the measured interval. This increases game
progress at a substantial visual cadence cost; the 15 FPS goal remains unmet.
The non-raster remainder by itself still exceeds the 66.67 ms budget.

Scanout reuse requires a prior drawn list, unchanged VI configuration/field,
a valid previous image, and no newly drawn list. CFB/framebuffer notifications,
mode changes and lifecycle resets refresh conversion. A CPU-only VI display
continues to update before its first display list. High-resolution ARM32 tests
cover these refresh safeguards. A skipped-list test checks no RDP framebuffer
writes, retained drawing state and exactly one FullSync interrupt.

The skip-three host battle completes 120 VI updates with 15 drawn and 45
skipped lists; its final CPU record and event queue exactly match the no-skip
baseline. RDRAM framebuffer differences are intentional. Captures are visually
inspected. A second 360-update run sends two button presses, transitions out of
the battle command menu, and retains exact CPU/event parity with no-skip.

`INTERP_ALU=1` now includes common load/store handlers and the direct RAM fetch
when combined with `INTERP_DIRECT=1 INTERP_MEMORY=1`. Every instruction is reread
from RAM; translated/unmapped fetches retain the original handler. Delay slots
and interrupts retain original execution boundaries. Each experiment passes
exact no-skip image/full-state parity and the full ARM32/high-resolution suite.
A NOP delay-slot shortcut passes host parity but measures only 0.06% gain
(3.8053 vs 3.8029); no meaningful gain is established.

The refreshed six-case SH4 software-only replay and strict GPU replay pass with
same-target texture/state comparison (`fps15-frameskip-reference.log` and
`fps15-frameskip-strict.log`). Fast's known failures remain unchanged.
Benchmark checker smoke checks reject restart, target error, impossible drawing
counts and changed frameskip; both 15-FPS threshold metrics still fail.

## Revised steadiness target

`measure_game_perf.py --start-frame 60 --end-frame 600 --metric vi
--window-frames 60 --min-fps 3.8 --min-window-fps 3.8` reports both the complete
average and each 60-update window, rejecting missing samples or a slower window.
Skip-two's warm frames 60-600 baseline measures 3.5765 VI updates/s and
0.5961 fresh drawings/s. Its slowest 60-update window is 3.1876, so the steady
3.8 target is not met. Skip-one remains to be measured.

Packed import processes two RGBA16 pixels per aligned native word, retaining
scalar fallback for halfword-aligned images and exact green-bit replication.
ARM32 and SH4 exhaustive import checks and full strict/reference replay pass.

Skipped tasks now retain vertex positions for CULLDL but omit task-local UV,
lighting and shade; validated triangles omit clipping/projection before their
suppressed draw. Culling, malformed triangle indices and FullSync are tested.
The full ARM32 suite and a 120-update skip-three image/full-state comparison
pass byte for byte. Its SH4 speed benefit remains to be measured.
The packed framebuffer import change preserves every RGBA16 channel and both
source/destination alignments in an exhaustive replay; Fast GPU import qualification and matched timing runs are pending.

## Local evidence and continuation

Private artifacts remain ignored under `build/dc/validation/`:

- `fps20-profile-game.log`, `fps20-second-tile-target.log`,
  `fps15-combined-target.log`, `fps15-padding-target.log`,
  `fps15-shade-target.log` record the measured intervals above.
- `fps20-host-baseline/`, `fps20-second-tile/`, `fps15-blender/`,
  `fps15-switch/` hold exact software images and saved-state comparisons.
- `fps15-replay-{strict,kos-reference,fast}.log` are the completed SH4 replays.
- `fps15-final-{host,reference}.log` are the completed ARM32 differential replays.
- `fps15-regression.log`, `fps15-switch-regression.log` record full host suites.
- `fps15-padding-regression.log` and `fps15-padding/` hold padding follow-up work.

Build the existing ARM32 Docker image `not64-dc-host:arm32`; Docker Desktop must
be running. Native LP64 builds are unsupported. DreamSDK lives at `C:/DreamSDK`;
run its bash with `DREAMSDK_HOME` set, source KOS's `environ.sh`, and use separate
build directories. SDK temporary/configuration access requires the normal
escalated build invocation. Use only isolated
`build/dc/validation/flycast-2.6/flycast.exe` processes; task-owned PID files are
recorded per experiment. Run console scraping in a separate PowerShell invocation
from native commands: its console detachment can invalidate their handles.

The player's existing launcher and images have not been replaced by experiments.

## Latest skip-two measurements

Matched warm 60–600 interval (540 VI updates), 90 drawn lists and 180 skipped:

| Variant | Progress updates/s | Slowest 60-update window | Fresh drawings/s | Raster ms/VI | Other ms/VI |
|---|---:|---:|---:|---:|---:|
| Skip-two baseline | 3.5765 | 3.1876 | 0.5961 | 125.61 | 145.11 |
| Packed framebuffer import | 3.6646 | 3.3003 | 0.6108 | 118.85 | 145.15 |
| Plus skipped geometry attributes/clipping | 3.9305 | 3.5126 | 0.6551 | 100.45 | 145.08 |
| Plus delay-slot RAM fetch | 3.9899 | 3.5615 | 0.6650 | 100.37 | 141.39 |
| Plus forced common-memory opcode inlining | 4.0669 | 3.6210 | 0.6778 | 100.32 | 136.69 |
| Branch inlining experiment (removed) | 4.0077 | 3.5737 | 0.6680 | 100.27 | 140.37 |

The average now exceeds 3.8 with skip-two, but the steady requirement remains
unmet. No timing threshold was relaxed. Logs are `fps38-import2-target.log`
and `fps38-rsp-skip2-target.log` under ignored validation. Skipped geometry saves
about 18.40 raster ms/VI compared with packed import alone. All 600-update
runs report zero invalid, unsupported, or failed graphics tasks.

A delay-slot RAM fetch helper retains each original handler and its interrupt
boundaries, reading current RAM instructions directly only under the same
KSEG0/KSEG1 first-4-MiB guard as the main loop. Other code retains `prefetch`.
ARM32/high-resolution suites pass, with exact no-skip 120-update battle image
and complete saved-state equality. SH4 delay fetch saves about 3.70 other ms/VI, and forced memory opcode
inlining saves another 4.70. These remain behind `INTERP_ALU=1`. Inlining
frequent branch handlers regresses the matched rate and has been removed.
An O3 version built and passed host parity, but was not timed or retained.
Neither retained shortcut changes emulated cycle counts.

## Alternating two/three skips

`--frameskip balanced`, saved Settings label `2 / 3`, and `GAME_FRAMESKIP=6`
select a seven-list pattern: draw, skip, skip, draw, skip, skip, skip. This
averages 2.5 skips after each draw, versus three in the previous setting. Modes
0–5 keep their fixed meaning; the default remains off. Changing mode or opening
a ROM resets the alternating phase. This is a fixed cadence, not adaptive
frameskip or proof of any target rate until measured.

Full ARM32/high-resolution suites pass, including the exact cadence across
14 lists and ROM reopen, every FullSync interrupt, and settings round-trip.
A 120-update software battle capture is visually intact and the saved CPU
record/event queue equals the no-skip baseline. Raster differences are expected
from the different drawing cadence. The matched 600-update SH4 run is underway.

### Battle qualification

The 600-update alternating run passes `--min-fps 3.8 --min-window-fps 3.8`
with 60-update windows over warm 60–600: **4.2842 progress updates/s**,
**3.8336 minimum window**, **0.6109 fresh drawings/s**. There are 77 drawn and
193 skipped lists over the measured interval. All nine windows pass; no invalid,
unsupported or failed graphics tasks are reported. The full 360-update two-button
menu-transition check also preserves exact saved CPU/event queue parity, with a
visually intact capture. A rental-menu scene and the final unlimited player
binary still need target timing before completion.

`build/dc/Play-Stadium2-Balanced.cmd` uses the new separate
`not64-stadium-balanced-gameplay.cdi`. It resumes the battle-commands checkpoint,
accepts live controls, and has no step/frame limit or scripted input/Start.
Its experimental fast renderer retains the four documented texture/state replay
failures; the full fast renderer is not qualified for strict pixel accuracy.
Physical Dreamcast timing and whole-game coverage are not established by these
Flycast checkpoint measurements. Existing launchers/images remain available.

### Rental-menu optimization history

The balanced policy meets the tested battle scene, but the rental menu remains
below 3.8. Warm interval 60–600, 60-update windows, empty replay:

| Rental build | Progress/s | Minimum window | Fresh drawings/s | Raster ms/VI | Convert ms/VI |
|---|---:|---:|---:|---:|---:|
| Balanced baseline | 1.8210 | 1.7676 | 0.2597 | 283.17 | 180.84 |
| Native aligned interlaced reads | 2.2074 | 2.1294 | 0.3148 | 283.18 | 84.72 |
| Plus per-field cache | 2.2116 | 2.1333 | 0.3154 | 283.08 | 83.91 |
| Plus native-width GPU (full transfers) | 2.3957 | 2.2904 | 0.3416 | 248.38 | 83.98 |

Other work stays near 85 ms/VI. Field-cache reuse brings little benefit here:
each rendered list refreshes both fields. Keep the VI/draw metrics separate.
Logs and JSON summaries are `fps38-{balanced,interlace,fieldcache,wide}-rental-*`.

Native interlaced reads specialize the pixel width, retaining byte-safe
unaligned input and exact fractional channel arithmetic. Full ARM/high-resolution
suites, aligned/unaligned format/crop/fraction/field comparisons and rental120
image/full-state equality pass. Per-field cache contract tests cover both fields,
fresh drawings, CPU writes, lifecycle and mode changes; rental120 parity passes.

`RASTER_WIDE=1` requires `RASTER_PVR=1 VIDEO_HIRES=1`. Its native 640-pixel GPU
canvas uses scissor-derived height and flushes before stride changes. It reserves
2304 KiB of main-RAM scratch and two maximum 1 MiB RTT buffers; output resolution
is preserved. Full native-width ARM optimized/reference, genuine SH4 software
reference and SH4 strict GPU replays pass. The strict native fixture checks every
640x480 word, preservation and 320→640→320 transitions with three GPU scenes.
Fast native qualification passes (four original scenes), but full fast retains
four known failures: combined-cold/hot, texture-precision (30) and texture-spans
(255). The checker remains nonzero; general fast pixel accuracy is unqualified.

Latest work is not yet timed: aligned integer/half-row RGBA16 conversion now
converts pairs and uses a channel-exact rounded RGB565 average. The exhaustive
65536-code/permuted-partner oracle, 96 aligned/unaligned fractional/crop/stride
comparisons, full ARM suite and rental120 image/full-state parity pass. Native
640-pixel general GPU batches also defer backdrop import until the affected
region is known, upload aligned row intervals before rendering, and read back
that region. The 320-pixel and strict sparse-fill paths keep their previous
transfer behavior. Software/texture-read barriers still flush before framebuffer
access. Transparent fills flush regional batches before relying on alpha outside
their bounds. Bounded fast RGB remains approximate within general geometry.

The expanded native fixture verifies outside-region RGB/alpha, blended backdrop
reads, odd horizontal edges, region reset and pending transparent fills. Its
final eight-scene fast native fixture passes with zero mismatches. Full strict
replay passes against the genuine SH4 software reference. Full fast still fails
the same four known groups. The region+packed rental600 benchmark is running. Final
battle regression and unlimited live-control player qualification remain required.
Existing player images have not been replaced. Physical Dreamcast and whole-game
coverage remain unverified; do not claim overall completion from battle alone.

### Steady policy qualification

Packed field conversion + regional transfers with mode 6 reaches 3.797433 avg,
3.640306 minimum window, 0.541486 fresh drawings/s, warm60–600. Raster160.575,
convert17.662, other85.040 ms/VI. Exact fill-span optimization produced no gain
and has been removed; the recorded span experiment repeats those rates.

New optional mode 7, CLI `--frameskip steady`, repeats skips 2,3,3,3 (four drawings
per 15 lists). It averages2.75 skips, less than fixed3, with maximum3. Every rolling
30-list interval has8draws, removing mode 6's8/9-draw window variation. Modes0–6
retain their meaning; settings label is `2 / 3 / 3 / 3`, defaultoff. Full ARM and
highres suites pass, including all FullSyncs, lifecycle resets, rolling counts,
max-gap and settings roundtrip7. Mode8 and changed-mode parser controls reject.
Rental120 and battle-menu360 CPU/event tails match no-skip; captures are intact.

Final mode 7 rental600 passes all nine windows at 3.959935 average / 3.919644 minimum,
with 0.527991 fresh drawings/s. The actual unlimited live-control player also passes
battle 60–600 at 4.284973 average / 3.827127 minimum / 0.571330 fresh/s. Its code includes
native VI/field reuse/region transfers and steady mode 7; no replay or autoStart,
no diagnostic limit. Battle phase costs: raster 88.493, convert 8.141, present 0.060,
other 136.680 ms/VI. Rental costs: raster 149.742, convert 17.657, present 0.059,
other 85.072. The revised checkpoint-scene target is met; broader platform/game
coverage is not established by these bounded measurements.
