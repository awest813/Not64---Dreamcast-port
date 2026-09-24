# Pokémon Stadium 2 compatibility work

Local USA ROM, 64 MiB, current 4 MiB emulated RDRAM. The ROM, native saves,
checkpoints, captures and disc images remain under ignored `build/dc/`.

## Implemented

- `VIDEO_HIRES=1`: optional 640×480 output capacity, 1024×512 RGB565 staging
  (1 MiB main RAM and two 1 MiB PVR scanout textures). Each interlaced VI
  updates its own field; fractional vertical source offsets interpolate adjacent
  rows. Mode/size changes clear the field history. This remains a preview, not
  the complete N64 coverage, divot or dither filter pipeline. Standard builds
  retain their existing 256 KiB staging allocation.
- Untextured shaded depth triangles now support the two-cycle combiner and
  blender path. Tests check both triangle halves, shade/primitive color,
  alpha rejection and near/far depth behavior.
- TextureRectangleFlip (`E5`) consumes both parameter commands and advances S
  down the screen and T across it. It falls back to software after flushing PVR.
  Tests cover transposition, clipping and truncated commands. Copy/fill modes
  remain invalid for this primitive; see the primary
  [libdragon rectangle documentation](https://libdragon.dev/ref/rdpq__rect_8h.html).
- Fill-cycle rectangles expand their inclusive endpoint before clipping. The
  previous order drew through y=240 despite a y<240 scissor, overwriting the
  following framebuffer descriptor with `00010001`. That caused VI origin
  `0000027f`, then a guest TLB exception. A 320x240 regression fixture protects
  the next allocation and checks right-edge clipping. This fixes the software
  fallback to agree with the existing PVR fill clipping order.
- Optional controller replay feeds normal Maple mapping/PIF input. It changes
  controller input only; it does not patch game memory or skip game scenes.

## Quick visual polish follow-up

TMEM odd-row word swapping now honors LoadBlock's DXT accumulator and tile
uploads for 4/8/16-bit textures. Stadium 2's DXT=0 HUD fonts are pre-swapped;
reading those as linear rows scrambled alternate glyph rows. The same-frame
host comparison now clearly reads level 50, Arcanine 164/164, Caterpie 116/116,
and the L/R button prompts. GPU texture baking uses the corrected sampler too.

RGBA16 copy mode also preserves the source alpha bit and uses it for enabled
alpha comparison, independent of blend-alpha threshold. Dither selection alone
does not enable comparison. This passes a targeted cutout/alpha test, but does
not change the measured Stadium battle frame; do not claim it fixed this HUD.
The behavior follows the [Angrylion copy rasterizer](https://github.com/ata4/angrylion-rdp-plus/blob/master/src/core/n64video/rdp/rasterizer.c).

Multi-row fixtures compare pre-swapped DXT=0 blocks, normal DXT blocks and tile
uploads for both one and two 64-bit words per row. Full high-resolution ARM32
regressions pass (`stadium-polish-regression.log`); the fast KOS build passes
(`stadium-polish-kos.log`). Private same-frame before/after images are
`stadium2-host/polish-before.png` and `polish-after.png`. The move-label concern was subsequently resolved by checking the held-R
controls below; broader effects/geometry accuracy still needs work.

## Text-panel and rectangle follow-up — 2026-09-22

Holding R reveals Flame Wheel, Extremespeed, Roar and Leer, with readable types
and PP in the ARM32 software run. The [Stadium 2 manual](https://pkproject.net/juegos/manual/manual-N64-Pokemon-Stadium-2-EN.pdf)
describes this held-R check view. The 120-frame run has zero invalid/unsupported
VI frames and zero decoder failures (`stadium2-host/check-moves.log/.png`).

Added optional recorded trigger pressures so the same UI state can be checked
on the fast target without game-memory changes. Parser/mapping tests cover the
legacy format, held R, dual-trigger C-up, exact event boundaries and malformed
or out-of-range optional fields.

Two-cycle fill rectangles now run both color-combiner/blender cycles instead
of being dropped. The per-pixel second-cycle path preserves combined feedback;
one-cycle fills keep their existing optimization. Tests check primitive-to-
combined color, transparent rejection and all scissor edges. This is a renderer
compatibility fix, not the cause of the move-panel behavior.

Fast SH4/Flycast also confirms all four names, types and PP:
`build/dc/validation/stadium-fast-move-text.png`. Two-cycle destination blending
passes the additional known-pixel check in `stadium-text-blend-test.log`.

Full high-resolution ARM32 regression: `stadium-text-regression.log`.
Fast target build: `stadium-text-kos.log`. Private held-R target image:
`not64-stadium-moves.cdi` (diagnostic, R released after 180 VI updates).
The normal gameplay launcher retains an empty replay and player control.

## Build and input replay

```sh
make -f Makefile.dc VIDEO=pvr VIDEO_HIRES=1 GFX=soft \
  RASTER_PVR=1 RASTER_STRICT=0 INTERP_DIRECT=1 PERF=1 LTO=1 \
  GAME_DISC=1 GAME_FRAMES=0 GAME_STEPS=0 GAME_START=0 \
  BUILD_DIR=build/dc/kos-stadium-fast TARGET=build/dc/stadium-fast.elf
```

For a reproducible checkpoint run, add `GAME_CHECKPOINT=1` and place the
matching private checkpoint at `/cd/boot.st`. Add `GAME_INPUT_REPLAY=1` only
when the test disc contains `/cd/input.txt`. Host runs accept
`--input-replay file` with or without `--load-slot N`.

Input text has one event per line:

```text
# VI-start duration Maple-button-mask-hex centered-X centered-Y
20 12 4 0 0
80 8 80 127 0
```

These examples press A, then right. Button masks: A=4, B=2, Start=8,
up=10, down=20, left=40, right=80; C-up=1000, C-down=2000,
C-left=4000, C-right=8000 (hex). Axes are -128..127; positive Y
is down. Durations are 1..600 VI updates, with at most 128 events. An empty
file provides no synthetic input. Optional sixth/seventh fields are left/right trigger pressure (0..255). For
example, `20 180 0 0 0 0 255` holds R through VI 199. Five-field files remain
valid. Trigger inputs pass through normal R/Z and dual-trigger C-shift mapping.
Inputs are relative to the diagnostic
run's VI counter, including when a checkpoint is loaded. Real controller
input continues through the same mapping.

## Validation commands

2026-09-24: fast one-cycle opaque fills now use exact color/masked readback and
avoid framebuffer imports. Three formerly failing fill fixtures pass; strict
and host suites pass. The paired battle checkpoint remains about 1.09 FPS.
See [video compatibility and frame costs](VIDEO_COMPAT_FRAMES.md).

### Cutout color precision — 2026-09-22

Fast PVR textures with only zero/full alpha now use ARGB1555, preserving five
bits per RGB channel instead of four. Fully opaque textures still use RGB565;
textures with any fractional **post-combiner** alpha retain ARGB4444. The cache
key and two-byte-per-texel allocation are unchanged. Strict texture fallback is
unchanged.

The ROM-free `texture-precision` replay exercises all 32 grayscale levels,
transparent regions, cold/hot cache draws, and a white RGBA16 texture made
partially transparent by primitive alpha. Dithering is disabled for this
precision fixture. In the same Flycast 2.6 configuration, each fast run completes
66 GPU scenes:

| Measurement | Before | After |
| --- | ---: | ---: |
| Exact draw mismatches (66 draws) | 42 | 30 |
| Total absolute five-bit RGB channel error | 124 | 74 |
| Largest channel error | 2 | 1 |
| Transparent-region errors | 0 | 0 |
| Partial-alpha errors | 0 | 0 |

The exact fast replay still fails: residual texture/readback rounding and the
previous fill/state discrepancies remain. Its checker deliberately retains
exact expectations. This is not a claim of corrected arena geometry, filtering
accuracy, physical hardware behavior, or higher FPS. Next: isolate the remaining
rounding error with an opaque texture ramp and imported-background probes before
changing readback or sampling rules.

Private evidence: `stadium-precision-baseline.log`, `stadium-precision-gpu.log`,
`stadium-precision-host.log` and `stadium-precision-regression.log` under
`build/dc/validation/`. The full high-resolution ARM32 suite and updated host
replay pass. `stadium-precision-strict.log` passes the exact replay checker with
GPU fill execution established; its textures use software fallback. The checker
still rejects the fast log. Both local fast images are rebuilt with this change.

Run the normal ARM32 `test` and `test-soft` targets, and repeat with
`VIDEO_HIRES=1`. `test-hires` independently builds the high-resolution VI
fixtures, including field preservation, fractional row access bounds,
RGBA5551/RGBA8888, blank transitions and native-size presentation.

## Gameplay evidence — 2026-09-21

The fast SH4 executable running in isolated Flycast 2.6 reaches Poké Cup,
Poké Ball Battle 1: Arcanine versus Nelson's Caterpie. The visible Battle,
Pokémon and Run menu is reached from a genuine pre-transition checkpoint.
The earlier fast run also completed recorded team selection and showed the
3D arena/Arcanine introduction. These are checkpoint-assisted target runs,
not an uninterrupted cold-boot-to-battle qualification. A separate fast run
loads the move-selection checkpoint, accepts C-up at VI 21 and visibly executes
"Arcanine's Flame Wheel!" with battle animation. Target gameplay is confirmed;
a complete turn/battle is not yet qualified.

The ARM32 software run traversed startup, title, Game Pak Check, central hub,
Stadium, Poké Cup, rental selection, team order and battle. Its six rentals are
Abra, Aipom, Alakazam, Ampharos, Arbok and Arcanine; the battle order selects
Arcanine, Abra and Ampharos. It accepts Battle and C-up, animates an attack and
reduces Caterpie's HP. The 600-frame attack run reports 301 display lists,
zero invalid/unsupported VI frames, and zero decoder failures.

Private evidence under `build/dc/validation/`:

- `stadium2-host/commands.log`, `moves.log`, `attack.log` and matching PNGs.
- `stadium2-host/checkpoints/`: named genuine checkpoints for each milestone.
- `stadium-battle.log`: fast target's corrected battle transition and GPU use.
- `stadium-attack.log`: separate fast target recorded C-up attack check.
- `stadium-fast-flame-wheel.png`: native Flycast window showing the attack.
  This is the fast SH4 build, not a host-generated picture.
- `stadium-complete-regression.log`, `stadium-complete-hires.log`: complete
  default and high-resolution ARM32 suites pass; default also runs `test-hires`.
- `stadium-battle-build.log`, `stadium-final-cold-build.log`: KOS fast builds.

The normal cold-boot image is `build/dc/not64-stadium-fast.cdi`. The interactive
battle checkpoint image is `build/dc/not64-stadium-gameplay.cdi`; it contains an
empty input replay so control stays with the player. `Play-Stadium2-Fast.cmd`
in that directory launches it in the isolated Flycast installation. The
separate battle/attack diagnostic images contain their recorded inputs.

Dreamcast A/B/Start map to N64 A/B/Start. Both triggers plus the D-pad select
N64 C-buttons; the analog stick navigates. Press A for Battle, then hold the
right trigger (N64 R) to show all four move names, types and PP. Release it to
return to the arena view. The earlier claim that move labels were missing was
incorrect: this is the game's intended held-R interface. The first move can be
selected with C-up. Other effects/geometry still need accuracy work.

AICA initializes (28805/32006 Hz observed across checkpoints), but sustained
audio quality remains unqualified. No 5 FPS claim is made. Fast rendering still
has its documented accuracy limitations. Expansion Pak behavior, complete
battles, uninterrupted cold-boot traversal and physical Dreamcast compatibility
remain outstanding. Keep the strict/software references for future accuracy
work; do not promote this fast result to strict qualification.
