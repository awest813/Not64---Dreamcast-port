# Pokémon Stadium 2 graphics accuracy — 2026-09-30

The latest opt-in small-texture filter grid builds on the triangle-sampling
and shaded-texture corrections below. It reduces battle-image error by another
0.25% against the software reference while retaining the latest speed improvements.
Game fidelity remains approximate. The measurements cover two checkpoints;
they do not establish overall gameplay performance or achieve the historical
8 FPS target. The follow-up qualification is recorded at the end of this report.

## Change and native accuracy checks

`RASTER_SHADE_SCALE=1` removes GPU packed-texel expansion from the multiplicative
vertex RGB of the four supported separable shade equations. RGB565 green and
ARGB1555 green use their respective packing scales. The additive bias and alpha
are retained. Fractional ARGB4444 keeps its existing path. No extra texture
memory, texture-cache entries, scene flushes or skipped frames are required.
The capability requires `RASTER_TEXEL_SCALE=1 RASTER_SHADE=1` and remains opt-in.

All four complete native replay suites pass: baseline fast, corrected fast,
strict and independent same-SH4 software. The new128-case color ramp covers
cold/hot draws, the four equations and RGB565/ARGB1555 storage. Accumulated
five-bit channel error decreases1104->460 (58.3%); maximum remains one step.
The existing varying/perspective triangle probe decreases75->55; its maximum
remains two steps. Exact texture/COMBINED/cache/span cases keep their exact
requirements. The checker rejects disabled correction, missing probes,
truncated completion and repeated build identity. The initial hot-blend oracle
compared against the wrong draw; its failing logs are retained as `*-oracle1.log`.
The final oracle stores independent cold/hot software results.

## Isolated performance comparison

Flycast2.6/OpenGL, GCC15.1/KOS2.3, private matching checkpoints, VI60–600,
nine60-update windows, CPU/SOFT/RASTER profiling disabled. Progress is presented
VI updates, with fresh drawing throughput separate. Performance runs use one
Flycast process. Untimed native/image comparisons may overlap.

| Scene | Baseline progress/s | Corrected progress/s | Corrected min window | Fresh drawings/s |
|---|---:|---:|---:|---:|
| Battle commands, corrected unlimited live player | 4.828490 | 4.828486 | 4.528641 | 0.643798 |
| Rental roster, matching corrected core | 6.799481 | 6.799471 | 6.679328 | 0.906596 |

Both retain mode7: skips2,3,3,3, maximum3 and average2.75. Each warm interval
has72 drawn and198 skipped lists. Throughput differs by less than0.0002%,
with no measurable loss. These are checkpoint menus, not a completed attack or
all scenes, and not physical Dreamcast measurements.

The separate player is `build/dc/Play-Stadium2-Accurate.cmd`, using
`not64-stadium-accurate-gameplay.cdi`. It starts at battle commands, accepts live
controls, has no scripted input/automatic Start and has no frame/instruction
limit. The original qualified `Play-Stadium2-Steady.cmd` and its disc are retained.
The player's actual VI60–600 run supplies the battle measurement above.

## Game-image evidence and remaining accuracy

Battle VI120 improves mean RGB-byte error23.42851->23.01378 (1.77%) against
the independent SH4 software renderer. There are65803 changed pixels out of
75840; maximum remains26 five-bit steps. CPU/event bytes and MMIO/SP/PIF hashes
match across baseline/correction/reference at the same update and draw cadence.
This is a modest color improvement; the fast renderer still has large image
differences. `accuracy-vs-reference-battle` and `prior-vs-reference-battle`
hold complete pixel comparisons.

A separate half-pixel triangle-origin experiment passed the current native
suite and lowered average battle error to20.30005. It increased the maximum
from26 to28 steps at two pixels, however, by changing triangle coverage. It
was rejected for the player and removed from production sources. Its source
snapshot, ELF/discs, native log and both game exports remain under ignored
`accuracy-triangle-*` evidence. Future sampling work should retain primitive
boundaries and adjust interpolated attributes independently.

Rental VI120 is pixel-for-pixel identical to the previous corrected fast build.
Its full CPU/event/MMIO/RDRAM/page/scanout state also matches. The shade correction
does not change that menu. Against the software image it still differs at126385
of303360 pixels, with mean RGB-byte error2.07547 and maximum8 five-bit steps.
The eight maximum-error pixels are at x84,y72–79; adjacent y71/y80 have four-step
errors. Packed-color shading probes alone do not fix this edge.

Remaining work includes filter-before-combiner versus baked texture filtering,
triangle sampling/coverage and framebuffer ownership. Strict replay equality is
against this port's software implementation, not proof of full N64 fidelity.

## Reproduction and evidence

`tests/dc/Makefile.raster-replay`: use separate `OUT` directories and
`WIDE=1 AFFINE=1 CUTOUT=1 IDENTITY=1 FULL_ALPHA=1 RECT_CLIP=1 RECT_SAMPLE=1
TEXEL_SCALE=1 SHADE_SCALE=1 BATCH_CACHE=1 CACHE_INDEX=1 IDENTITY_SPAN=1 COMBINE_SPAN=1`.
Use `--gpu --wide --affine --identity --cutout --batch-cache --full-alpha
--rect-clip --rect-sample --cache-index --identity-span --combine-span
--shade-scale --reference <same-SH4-software-log>` in `check_raster_replay.py`.

Ignored `build/dc/validation/accuracy-*` includes all native serial logs,
checker controls, game timing JSONs, full state/pixel exports and image
comparisons. `accuracy-manifest.json` records build flags, backend identity and
ELF/disc/ROM/checkpoint/launcher hashes. Private game data stays outside Git.

## Follow-up: triangle sampling without moving geometry

The opt-in `RASTER_TRI_ORIGIN=1` correction keeps triangle x/y unchanged and
shifts their interpolated attributes from pixel centers to the software
renderer's integer sample origins. It shifts U/W, V/W and reciprocal-W together
before recovering UVs, so perspective interpolation remains consistent. The
same shift applies to supported RGB shade planes; alpha is preserved. Invalid
geometry, nonfinite attributes, nonpositive shifted reciprocal-W and shades
outside the representable range retain the previous GPU attributes. Rectangles
retain the existing qualified submission path and sampling correction. No
additional texture allocation or software fallback is introduced.

The corrected optimized native replay passes all existing fixtures and the new
40 projection/rejection checks, four complete framebuffer footprint comparisons
and four exact interior texture-ramp comparisons. These exercise seven actual
corrections and 12 completed GPU scenes. The baseline fast, strict and software
reference suites use the same SH4 target and LTO configuration. The corrected
non-LTO suite also passes. Varying/perspective shade channel-error sum improves
55->34, maximum two->one five-bit steps. Six checker negative controls reject
incomplete, disabled, repeated and deliberately failing evidence.

Battle VI120 mean RGB-byte error against the same independent software image
improves 23.013783->20.697736 (10.0637%). Changed pixels fall from 65803 to 65289
out of 75840, and maximum remains 26 five-bit steps. The corrected game reports
2455 corrected and 910 guarded triangles by 120 submissions. Exact
CPU/event bytes and MMIO/SP/PIF checksums match the current fast and software
captures. Rental VI120 reports no triangle corrections and remains identical
in every pixel and in its full audited state. Its software-reference error
remains 2.075474, maximum 8 steps.

The correction improves error at 21133 battle pixels, worsens it at 15437,
and leaves 39270 with equal error. Each of four horizontal bands improves
in aggregate; rows 50–99 improve most (29.48525->22.60425 mean RGB-byte error).
This is an aggregate improvement against the software implementation, not
pixel-exact equivalence or proof of original N64 hardware fidelity. Baked
texture filtering and remaining edge/quantization differences still matter.

An earlier implementation of the shared submission lambda passed the old
native fixtures but corrupted rectangle UVs in game captures: rental mean
error 17.9249 and battle 40.5359, both maximum 31 steps. It was rejected. Restoring
the original floating-argument submission for rectangles and guarded triangles
resolved the captured regression. `accuracy-origin1-*` preserves the rejected
game exports; `accuracy-origin2-*` preserves the corrected full exports and
comparisons. No compiler cause has been established. The earlier rejected
geometry-shift experiment remains rejected separately.

The actual unlimited player and the matching rental build complete isolated
VI60–600 runs, with nine 60-update windows and exactly one Flycast process:

| Scene | Baseline progress/s | Corrected progress/s | Baseline min window | Corrected min window | Corrected fresh draws/s |
|---|---:|---:|---:|---:|---:|
| Battle commands, actual unlimited player | 4.828063 | 4.825477 | 4.528632 | 4.523288 | 0.643397 |
| Rental roster, matching corrected core | 6.799471 | 6.800327 | 6.679311 | 6.679304 | 0.906710 |

Battle average and minimum rates decrease by 0.0536% and 0.1180%, respectively.
Rental average increases by 0.0126%; its minimum changes by 0.0001%. Both
scenes retain baseline average/minimum throughput within 0.2%, with 72 drawn
and 198 skipped lists, steady mode7 and skips 2,3,3,3. Every measured window
exceeds 3.8 game-progress updates/s. These remain checkpoint measurements in
Flycast, without completed-attack, all-scenes or physical Dreamcast coverage.

The first player timing capture completed but was excluded because two idle
emulator validation processes remained open; the partial rental baseline also
overlapped the running player and was aborted. A slash/backslash mismatch in
the cleanup path comparison caused this. These captures remain as
`accuracy-origin-player-battle-overlapped-*` and
`accuracy-origin-base-perf-rental-aborted-target.log`, and do not qualify speed.
The original battle baseline ran alone and remains valid. The isolated rerun
of the player reproduces its timing samples exactly; the excluded capture
is retained as diagnostic evidence only.

The new separate player is `build/dc/Play-Stadium2-Accurate-Origin.cmd`, using
`not64-stadium-accurate-origin-gameplay.cdi`. It starts at battle commands,
accepts live controls and has no input replay, automatic Start or frame/step
limit. Both previous players remain byte-for-byte preserved. Build flags,
native validation, full image/state exports and private checkpoint hashes are
recorded in `accuracy-origin-manifest.json` and ignored `accuracy-origin*`
evidence. The new player build initially overwrote the earlier
`accuracy-player-build.log` and package log; its output is copied to separate
`accuracy-origin-player-*` files and the script is corrected. The earlier
player's compiler flags and hashes remain in its original manifest.

## Follow-up: small filtered shade textures

The opt-in `RASTER_FILTER_GRID=1` capability requires `RASTER_TRI_ORIGIN=1
RASTER_SHADE_SCALE=1`. Three supported opaque-source shade equations bake the
software three-point texture filter on a quarter-texel grid when native padded
dimensions are at most 64x64. Logical UV dimensions remain unchanged; the
physical texel-center offset follows the larger grid. Bilinear GPU sampling
then interpolates within a smaller portion of each three-point diagonal.
Geometry and shade alpha remain unchanged. Point/average filters, larger
textures, rectangles and binary cutout shade retain their native texture grids.
This is still an approximation, and unsupported GPU operations remain rejected.

The new 12-case native fixture compares the software image, native GPU grid
and corrected grid. Eight high-contrast cases cover cold/hot draws, all three
equations, fractional coordinates and affine/perspective UVs. Four controls
compare the entire cold and hot framebuffer with correction toggled. Enabled
builds require actual grid bakes and hot cache reuse, 48 completed GPU scenes,
at least 50% aggregate improvement and a peak bound of five five-bit steps.
Existing exact checks and tolerances are unchanged. Measured aggregate error
falls 7453->754 (89.8833%), with maximum 16->4 steps. All four complete LTO
corrected/baseline/strict/software suites pass. Nine negative checker controls
reject missing, disabled, repeated, failing or insufficient-improvement evidence.

Battle VI120 mean RGB-byte error improves 20.697736->20.644967 (0.25495%) against
the same software image. Changed pixels decrease 65289->65100 of 75840;
maximum remains 26 steps, at five pixels. Exact CPU/event bytes and MMIO/SP/PIF
hashes match the previous fast and software captures. Rental VI120 remains
identical in every pixel and in its entire audited state. The battle capture
uses eight filtered texture bakes, with texture VRAM 126080->329600 bytes
(+198.75 KiB), no additional misses, evictions or pinned flushes. Rental uses
zero grids and retains 403968 bytes. The existing 2 MiB cache budget remains.

The initial half-texel grid, allowed through 128x128 native dimensions, reduced
focused aggregate error but reached nine steps and failed the original bound.
Its battle mean error regressed to 26.217506. It was rejected. The corrected
quarter-texel grid is limited to 64x64. `accuracy-grid1` through `accuracy-grid4`
keep the rejected binaries and diagnostic logs; the first focused fixture had
two incorrect shader selectors, fixed before qualification. The current
`accuracy-grid6` replay strengthens the controls to compare both cold and hot
outputs. Its audit-disc executable, ROM, input and checkpoints are byte-identical
to `accuracy-grid5`; their complete game images also match in every pixel.

The first full speed comparison exposed a 0.3508% rental slowdown despite
zero filtered grids, exceeding the existing 0.2% preservation bound. This
version is rejected for the player. Native texel and triangle paths now avoid
division by the grid factor; the four-times grid uses exact power-of-two
multiplication only when active. The optimized `accuracy-grid7` passes all
four complete LTO suites and all nine negative checker controls. Both complete
game images match `accuracy-grid6` in every pixel, and rental audited state
matches the previous qualified player. All evidence and private data remain
under ignored build output.

## Filter-grid player qualification

Flycast 2.6/OpenGL, current GCC 15.1/KOS 2.3 LTO builds, one emulator per
run. Both rebuilt baseline discs were remeasured after the native-path
optimization. Battle measures the actual unlimited live-control player;
rental uses the matching corrected core with its private roster checkpoint.

| Scene | Baseline progress/s | Filter-grid progress/s | Filter-grid min window | Fresh drawings/s |
|---|---:|---:|---:|---:|
| Battle | 4.825041 | 4.823754 | 4.520649 | 0.643167 |
| Rental | 6.803749 | 6.796910 | 6.680265 | 0.906255 |

Battle average changes -0.0267% and minimum
changes -0.0582%. Rental average changes -0.1005% and minimum
changes +0.0143%. Both retain average and minimum
throughput within 0.2% of the matched baseline. Nine VI60–600 windows per
scene exceed 3.8 progress updates/s. Both use 72 drawn / 198 skipped lists
with steady mode7, skips 2,3,3,3, maximum3 and average2.75.

First 60 updates including cold texture bakes: battle 4.126346
-> 4.019075 progress/s; rental 6.281475
-> 6.268425. Battle adds about 0.39s of initial work.
Both remain above 3.8 in this initial interval. These are checkpoint
measurements, without completed-attack, all-scenes or physical Dreamcast
qualification.

The separate live-control player is `build/dc/Play-Stadium2-Accurate-Filter.cmd`
using `not64-stadium-accurate-filter-gameplay.cdi`. It starts at the battle
checkpoint with no input replay, automatic Start or frame/step limit. Steady,
Accurate and Accurate-Origin players and their recorded evidence remain
byte-for-byte preserved. Full logs, images, state comparisons, final source
snapshot and SHA256 provenance are under ignored build output, including
`accuracy-filter-manifest.json`. All validation emulators were closed.
