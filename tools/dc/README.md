# Dreamcast build and graphics foundation

The core currently requires a little-endian ILP32 ABI (32-bit `int`, `long`,
and plugin words). Native 64-bit macOS/Linux builds now fail with an explicit
diagnostic rather than silently giving RDRAM and SP memory the wrong layout.
This is a supported-configuration restriction, not a completed LP64 port.

See [Experimental PVR geometry](PVR_RASTER.md) for the optional hardware
rasterizer, required Flycast readback settings, and validation limits.

## Host regression tests on macOS or Linux with Docker

From the repository root:

```sh
make -f Makefile.dc help
make -f Makefile.dc host-test
```

`host-test` builds `tools/dc/Dockerfile.host` and runs the ILP32 suite. Equivalent
manual Docker:

```sh
docker build -t not64-dc-host:arm32 -f tools/dc/Dockerfile.host tools/dc
docker run --rm -v "$PWD:/workspace" not64-dc-host:arm32
```

The image builds 32-bit ARM Linux executables and runs them with QEMU userspace.
It does not emulate Dreamcast hardware. The Ubuntu base is pinned by digest;
APT packages follow that release's repositories. The compiler used for the
2026-09-19 validation was GCC 13.3.0 (`13.3.0-6ubuntu2~24.04.1`).

Expected results:

- `memory contract + graphics lifecycle PASS`
- `CPUTEST PASS`, PIF/controller and AI/save smokes
- Bounded VI converter and software expansion tests pass
- Exact PPM capture matches all pixels and its recorded SHA-256
- Dummy IPL completes its bounded run
- `VITEST PASS (76800 CPU-written pixels + VI scanout)`

Objects and executables live in `build/dc/host-arm-linux-gnueabihf/`. To run a
specific ROM with an explicit interpreter iteration budget:

```sh
docker run --rm -v "$PWD:/workspace" not64-dc-host:arm32 \
  qemu-arm -L /usr/arm-linux-gnueabihf \
  build/dc/host-arm-linux-gnueabihf/not64-dc-bringup roms/dc_vitest.z64 300000
```

The default budget remains 10,000, suitable for CPUTEST/dummy. VITEST needs the
larger budget. An incomplete VITEST fails even when execution exits cleanly.
Idle-loop optimization advances to the next emulated interrupt, so callback
counts are not retired instruction counts, elapsed video frames, or measured FPS.

The test executable checks CPU bus writes against graphics and actual RSP
accessors, SP IMEM offset, duplicated RSP plugin structure layout, last legal
RDRAM reads, out-of-range/misaligned reads, initialization failures, counters,
close/reopen, and absence of synthetic DP interrupts from the diagnostic backend.

## Dreamcast ELF

With an installed KOS toolchain:

```sh
source /opt/toolchains/dc/kos/environ.sh
make -f Makefile.dc -j4
```

On this workstation an existing SDK image was available. The following local
image ID reproduces the SDK selection used for validation:

```sh
docker run --rm -v "$PWD:/workspace" -w /workspace \
  --entrypoint /bin/bash \
  sha256:7ce19827478d70e75f5180b2c238b947b6d9f598c514ed56d5ad364bfd0fb4ab \
  -c 'source /opt/toolchains/dc/kos/environ.sh >/dev/null; make -f Makefile.dc -j4'
```

That ID is a local image, not a publicly pullable SDK. Its local tag is
`bloom-dreamcast-sdk:gcc15.1`. Recorded provenance:

- SH compiler: GCC 15.1.0; `-m4-single` (64-bit double), little-endian.
- KOS checkout: `804b3195ebd1a06a27cc2b3a5eacf7a2429040a3`.
- KOS checkout contains an existing nine-line `kernel/thread/mutex.c`
  compatibility wrapper exporting `_mutex_lock`; it was not changed here.
- SHA-256 of `git diff -- kernel/thread/mutex.c` in that checkout:
  `fda55a499e1395527e02d32661a8fc7e29e4bf0fe20357423c5ef8d11e8ccb96`.

Default output is `not64-dc.elf`, with `build/dc/kos-software/not64-dc.map`.
`VIDEO=pvr` produces `not64-dc-pvr.elf` and `build/dc/kos-pvr/`. Host and KOS objects
are separate; generated dependency files track header changes and a compiler
flags stamp tracks option changes. GCC 15 build blockers were corrected by
using a byte pointer for ROM header loading and declaring the DC invalidation
stub, rather than disabling the compiler diagnostics.

The ELF links as 32-bit little-endian SuperH, entry `0x8c010000`. This SDK marks
objects with ELF flag `sh4a`; both presenter demos boot and render in Flycast,
but physical Dreamcast compatibility remains unverified.

KOS/newlib defines `_BIG_ENDIAN=4321` even on little-endian SH4. Legacy core
branches now explicitly select the Dreamcast little-endian path for CPU register
views, bus memory, floating-point register pointers, and RSP accesses. Before this
fix, the real target entered an exception path and presented zero frames while
the host test passed. Compile-time memory-layout assertions and an optimized
host regression with that constant defined protect the fix. The build also uses
`-fno-strict-aliasing` for legacy overlapping register and memory views.

## Self-contained Flycast demos

Add `DEMO=1` to either KOS build. It embeds the generated fixture in a KOS ROM disk,
runs 300,000 interpreter iterations or 120 valid frames, checks the result, then
performs eight close/reopen cycles with 19 valid frames each. It holds the final image for 60 seconds, then closes the backend, restores the console, and idles until you close the emulator.
No SD card or game dump is required. Example using the local SDK:

```sh
docker run --rm -v "$PWD:/workspace" -w /workspace --entrypoint /bin/bash \
  bloom-dreamcast-sdk:gcc15.1 \
  -c 'source /opt/toolchains/dc/kos/environ.sh >/dev/null; make -f Makefile.dc VIDEO=pvr DEMO=1 -j4'

/Users/allenwest/Downloads/Flycast.app/Contents/MacOS/Flycast \
  -config config:Debug.SerialConsoleEnabled=yes \
  -config config:rend.EmulateFramebuffer=yes \
  "$PWD/not64-dc-pvr-demo.elf"
```

Use `VIDEO=software` and `not64-dc-software-demo.elf` for the software baseline.
CLI options are transient and do not change saved Flycast preferences. Framebuffer
emulation is enabled for the direct software display. Ordinary builds accept
`[rom.z64 [positive-step-budget]] [--frames positive-count] [--capture output.ppm]`;
arrange ROM storage for the loader. Target defaults to 120 valid frames; host has
no default frame cap. Injected controller/PIF/AI smokes run only on the host.

Verified on 2026-09-19 with the Flycast app in Downloads: both backends visibly
showed the eight bars with corner markers and logged:

```text
VITEST PASS (76800 CPU-written pixels + VI scanout)
Graphics software: VI=121 presented=120 DList=0 RDP=0 invalid=0 unsupported=0 failures=0
Graphics pvr: VI=121 presented=120 DList=0 RDP=0 invalid=0 unsupported=0 failures=0
```

PVR reported 262,144 bytes each for main-memory staging and texture allocation,
and 6,544,072 bytes remaining in its VRAM allocator. These are not whole-system
heap measurements. One Flycast launch failed its own host memory-layout assertion;
a fresh launch succeeded. Emulator evidence does not establish hardware timing.

## Graphics scope

`platform/dc_gfx.c` owns lifecycle, counters, checked RDRAM access, VI conversion,
and presentation. The converter supports bounded RGBA5551 and RGBA8888 input,
integer crop offsets, row stride, blanking, and native extents up to 320×240.
Interlacing, fractional extents/crops, reserved modes, and invalid source ranges
are rejected. Output is a centered 2× nearest-neighbor preview; VI filtering,
gamma, AA, divot, and accurate analog viewport reconstruction are not implemented.

The software backend expands RGB565 into a double-buffered 640×480 display.
The PVR backend uses one opaque quad and a 512×256 linear RGB565 texture.
Uploads cover used 512-wide rows (`height × 1024` bytes for a 240-line
frame), not the unused padding. It drains queued TA work, then rendering,
before texture reuse or release. Both temporarily move framebuffer console
output to serial and restore the prior display/logging state on shutdown.

With default `GFX=none`, raw RDP/display lists emit unsupported diagnostics.
`GFX=soft` adds the experimental game path described below; raw DPC remains
unsupported. The color-bar tests alone prove framebuffer scanout, not game rendering.

The fixture is emulated MIPS code, not a host-painted test image: it writes eight
bars and distinct corner pixels into RDRAM at `0x10000`, programs VI, and writes
a completion marker at `0x200`. The host harness validates every source and output
pixel. PPM SHA-256:
`5f501e6be1b53ce45079a6e4d256a32be602f70c2ac5ce6b1af8dd1f05cf7617`.

Additional regression for the SDK byte-order constant at optimization level 2:

```sh
docker run --rm -v "$PWD:/workspace" not64-dc-host:arm32 \
  make -f Makefile.dc HOST=1 CC=arm-linux-gnueabihf-gcc \
  'RUNNER=qemu-arm -L /usr/arm-linux-gnueabihf' \
  BUILD_DIR=build/dc/host-endian-regression \
  'CFLAGS=-std=gnu11 -fgnu89-inline -O2 -D_BIG_ENDIAN=4321' test
```

## Lifecycle stress and timing (Flycast)

Both embedded demos passed eight reopen cycles (272 valid frames including the
initial CPU run). Cycles alternate ROM close/reopen and complete graphics plugin
teardown/reinitialization, double-close to exercise idempotence, and blank,
invalid-origin, and unsupported-mode transitions followed by valid recovery.
Every cycle checks all 76,800 source pixels. Expected invalid-mode diagnostics
are intentional during this test. PVR free texture memory stayed at 6,544,072
bytes across all nine initializations. This checks allocation stability, not a
whole-system heap leak audit. Both final images were visually verified; the final
shutdown restored the display mode.

Measured means from the emulated KOS clock:

| Measurement | Result |
| --- | ---: |
| PVR conversion + presentation call, 128 steady stress frames | 25,029 us |
| Software conversion + presentation call, 128 steady stress frames | 33,441 us |
| PVR full 256 KiB texture upload, stress-cycle means | approximately 659–661 us |
| PVR scene submission, stress-cycle means | approximately 15 us |

Used-row uploads now DMA `height × 1024` bytes (245,760 for 240 lines) instead of
the full 262,144-byte texture. The table above is the earlier full-texture
baseline; re-measure on Flycast before quoting a new upload mean.

The combined call includes CPU conversion, scheduling, and display/render waits.
The scene submission time is CPU enqueue cost, not GPU completion latency.
PVR wait/upload/submit counters are enabled only in embedded diagnostic builds.
These are one-run emulator measurements, not FPS claims, controlled host wall-time
benchmarks, or real Dreamcast performance predictions.

After the 60-second hold and final shutdown, validate a captured serial log with:

```sh
python3 tests/dc/check_target_log.py pvr path/to/pvr.log
python3 tests/dc/check_target_log.py software path/to/software.log
```

The checker requires all nine pixel checks, all eight cycles, correct initial
counters, final shutdown evidence, and (for PVR) stable allocation and complete
timing records. Local evidence is in `build/dc/validation/*-stress.log`.

Next: the bounded raw RDP rectangle reference path in `PVR_PLAN.md`, plus physical
hardware validation when available.

## Experimental game renderer (`GFX=soft`)

`GFX=soft` connects the existing C++ software rasterizer to RSP graphics tasks,
then sends its RDRAM framebuffer through the same VI converter and PVR presenter.
It supports the F3DEX2 path exercised by Mario Golf (USA), including matrices,
lighting, textured triangles, texture rectangles, fills, palettes, and FullSync.
It is a software renderer with PVR presentation, not accelerated N64 geometry.
The default `GFX=none` remains the diagnostic framebuffer-only build.

The game boot work also corrected ROM header/region byte order, normalized all
three dump formats, guarded reset-time VI reads against division by zero, and
serialized controller replies in Joybus byte order rather than compiler bitfield
order. Fixture-only input/audio/PIF smokes no longer modify commercial-game RAM.

Build a **local test disc** using a ROM you already have:

```sh
tools/dc/build_game_disc.sh '/absolute/path/to/game.z64'
```

This uses the pinned local SDK above and outputs `build/dc/not64-game.cdi` plus
`not64-dc-pvr-soft-game.elf`. The ROM is streamed from `/cd/game.z64` through the
1 MiB cache rather than embedded in Dreamcast main RAM. Both the copied ROM and
disc are under ignored `build/`; do not publish either. Exit the prior test
instance before rebuilding its disc image. `NOT64_DC_SDK_IMAGE` can override the
SDK image when the recorded local image is unavailable.

For this workstation:

```sh
/Users/allenwest/Downloads/Flycast.app/Contents/MacOS/Flycast \
  -config config:Debug.SerialConsoleEnabled=yes \
  -config config:rend.EmulateFramebuffer=yes \
  "$PWD/build/dc/not64-game.cdi"
```

The game-disc preset runs up to 600 valid frames / 500 million interpreter loop
iterations, pulses Start during VIs 400–419, holds the last image for 60 seconds,
and releases graphics/CPU/cache resources before idling until you close the emulator. It is a bounded display regression, not a normal game launcher.
A different game may need different timing. Standalone builds accept `--frames`,
`--capture output.ppm`, and `--start-at VI`; no synthetic input occurs without
that option outside the game-disc preset. The existing Maple input stays active.

Host renderer checks (also run the default regression suite above):

```sh
docker run --rm -v "$PWD:/workspace" not64-dc-host:arm32 \
  make -f Makefile.dc HOST=1 GFX=soft CC=arm-linux-gnueabihf-gcc \
  'RUNNER=qemu-arm -L /usr/arm-linux-gnueabihf' test-soft
```

The focused checks cover adjacent RGBA16 texels, I4 and CI4 palettes, bounds,
copy coordinates, tile wrapping, color saturation, three-point filtering,
transparent color/depth rejection, blending, F3DEX2 fill/FullSync, malformed and
recursive display lists. They also pass with `-fsanitize=undefined
-fno-sanitize-recover=all` in a separate build directory. Unsupported commands
stop the run and increment `decode-failures`; they must not be reported as a
successful game run. SP completion belongs to RSP HLE; software FullSync owns
its DP interrupt. The separate raw-DPC path is still unsupported.

Limitations: 4 MiB RDRAM only; no raw RDP triangles or general microcode coverage;
no accurate TMEM DXT/odd-line interleaving, RGBA32 split banks, YUV/keying, coverage
buffer, or exact RDP blending/VI filters. Alpha-to-coverage is approximated for
cutouts, and alpha dithering uses a deterministic pattern. Rendering is slow;
physical Dreamcast performance and compatibility have not been established.
The software depth LUT and working-state allowance add 544 KiB to the paper
main-RAM budget. See [PVR_PLAN.md](../../PVR_PLAN.md) for later acceleration gates.


### Game validation record — 2026-09-19

Local ROM: Mario Golf (USA), header `MarioGolf64`, country `0x45`, 32 MiB.
The final host title run completed 658 VI callbacks / 600 presented frames /
302 display lists, with zero invalid/unsupported VI modes, presentation failures,
or decode failures. A second 370-frame run confirms that tree cutouts, sky, and
scene colors no longer show the earlier block/neon artifacts. Captures were
visually inspected; this is not a pixel-exact comparison to N64 hardware.

Local evidence (ignored build outputs):

- `build/dc/mario-golf-blend-fixed.png`: final title-screen host capture.
- `build/dc/mario-golf-final-scene.png`: final opening-scene host capture.
- `build/dc/validation/host-game-regression.log`: CPU, memory, PIF/Start, VI,
  exact fixture image, and all three cartridge byte-order checks.
- `build/dc/validation/soft-ubsan.log`: renderer regressions under UBSan.
- `build/dc/validation/mario-golf-host.log`: 600-frame title run.
- `build/dc/validation/mario-golf-scene-host.log`: 370-frame scene run.

Final title PPM SHA-256:
`843a04d85030f74189e654c691949d6b4bd1de46ee049529d22917e1f8600637`.
The game ROM and derived disc/captures are not checked in as test fixtures.

The same game disc was visually verified in the live Flycast/PVR window: full
Mario Golf title artwork, natural colors, clean cutout characters, and blinking
PRESS START. ELF SHA-256:
`369700b60ff2c852028af08fdd0a8820a5edf388f65dad4c3a8fb3deef645dd6`.
Local disc SHA-256:
`b0aebfb13839b52c2c7b98ab0510be6620931cbaed85c11fbdccc8e7734bd92d`.
This verifies the opening/title display checkpoint, not full gameplay or speed.

The Flycast run completed 658 VI callbacks, 600 presented frames, and 302 display
lists with `invalid=0 unsupported=0 failures=0` and `decode-failures=0`.
Serial evidence: `build/dc/validation/mario-golf-flycast.log`.

Stopping-checkpoint update (2026-09-20): the complete serial log later records a
second KOS startup and `Fatal: SH4 exception when blocked`, after the successful
600-frame run and display-mode restoration. This exit/restart failure is unresolved;
do not interpret the zero in-run error counters as proof of clean disc shutdown.


### Branch integration — 2026-09-20

The graphics work and performance/menu branch are combined on `master`. The
ROM browser runs by default on ordinary Dreamcast builds, or with `--menu` on
the host; explicit ROM paths and the demo/game-disc presets bypass it. Its
buffer is released before graphics starts. Controller input uses KOS's centered
stick coordinates, N64 scaling, and explicit Joybus packet serialization.
All dump formats share the same header decoding after cache normalization.

Host validation includes the combined CPU/PIF/menu/TLB/VI suite, exact pixel
capture, all three dump formats, an optimized SDK-endian regression, and the
software renderer tests. A generated 3 MiB fixture also exercises ROM-cache
replacement and persistent file handling without a commercial game dump.

The standalone ELF returned to Flycast's BIOS successfully, but the same code
on a disc revealed that Flycast's emulated BIOS (ReIOS) boots a mounted disc
again when asked for its system menu. This reproduces the repeated startup
that preceded the old game-disc crash; changing KOS's exit destination alone
does not fix it. Diagnostic demo/game builds now release graphics, CPU, and
cache resources and remain in a sleeping KOS idle loop. Close the emulator
after the completion message. Ordinary builds request the system menu on exit;
with ReIOS and a mounted disc that may boot the browser again.

The target log checker rejects fatal errors and repeated startup and requires
a successful cleanup/idle message, along with all existing presentation checks.

Validated the final diagnostic disc in Flycast: nine complete pixel checks,
272 presented frames, eight reopen cycles, stable PVR allocation, and
`Dreamcast run complete: status=0; diagnostic idle` with one KOS startup.
`python3 tests/dc/check_target_log.py pvr build/dc/validation/merged-disc-idle.log`
passes. The validator also rejects both the historical fatal game log and the
intermediate BIOS-menu disc that restarted. The final game-disc ELF builds;
its complete 600-frame commercial-game run was not repeated after integration.

## V2 upload skipping and A1 audio (2026-09-20)

Normal KOS builds now output stereo PCM16 through `snd_stream`. The installed
KOS API uses a callback and `snd_stream_poll`, not `snd_stream_push`. The DC
plugin is statically linked; its 5 ms worker calls `aiUpdate` independently of
slow emulated VI frames. It handles stereo halfword order, silence on underrun,
rate changes, overlay pause/resume, mute and joined ROM-close cleanup. The ring
still drops old data when full; this does not throttle the interpreter to audio.
Hardware listening and commercial-game audio quality have not been verified.

`DEMO=1` now plays short tones and requires an `AICA stream PASS` line after
three drain/pause/rate/underrun/reopen cycles. The host suite also checks PCM
channel/byte order, ring wrap, underrun silence and latest-DMA accounting.

To measure V2, add `UPLOAD_SKIP=1` to a `VIDEO=pvr` build. Identical converted
rows and dimensions reuse the last texture; the PVR still submits the quad.
The stress loop changes/restores pixels after repeated static frames and checks
blank/invalid recovery and eight reopen cycles. The serial validator expects
one initial upload and three uploads per stress cycle with skipping enabled,
or the original 120/19 upload counts when disabled.

V2 stays **off by default**: Flycast measured about 3.7 ms/frame for XXH32,
versus 30–31 us for the existing DMA upload. This saves transfer traffic but
regresses emulator frame time; measure physical SH4 before enabling it normally.
The `hash-avg-us` field is separate from DMA timing.

Windows DreamSDK build (run with access to the SDK's temporary directory):

```powershell
$env:DREAMSDK_HOME = 'C:\DreamSDK'
& C:/DreamSDK/usr/bin/bash.exe -lc 'source /opt/toolchains/dc/kos/environ.sh >/dev/null 2>&1; cd /f/GitHub/Not64---Dreamcast-port && make -f Makefile.dc VIDEO=pvr DEMO=1 UPLOAD_SKIP=1 -j8'
```

Launch Flycast with `-config config:Debug.SerialConsoleEnabled=yes` and
`-config config:rend.EmulateFramebuffer=yes`. Use its normal SH4 dynarec for
comparison with V1; the interpreter setting gives different timing results.
Resize serial capture early using `tools/dc/scrape_console.ps1 -Mode resize`.
Wait for diagnostic idle after the 60-second image hold, capture the complete
log, then run `python tests/dc/check_target_log.py pvr <serial.log>`.

Local evidence: `build/dc/validation/v2-a1-host.log`, `v2-a1-flycast.log`
(upload skipping), `v2-a1-default.log` (normal DMA), and the matching build logs.
These generated logs and ELFs remain local and ignored by Git.

Audit polish: oversized audio DMA now retains the newest ring with at most
64 KiB copied under the mutex. Rate changes discard old-rate PCM, while a
same-rate notification preserves queued samples. Successful save-state loads
flush pre-load audio and restore the AI clock without unpausing the overlay.
Host regressions cover those cases and end-of-RDRAM clamping. The target audio
test now submits through the public DMA/DAC-rate functions and exercises mute
and unmute in addition to pause, paced drain and reopen. PVR rejects further
presents if recovery from a failed submission cannot drain rendering safely.
The RAM budget includes the additional 16 KiB of audio output buffers.

Audited-run evidence: `build/dc/validation/audit-host-final.log`,
`audit-flycast.log` (upload skipping), `audit-default.log` (normal DMA), and
`audit-default-build.log` / `audit-game-build.log`.

## Zelda Ocarina of Time boot (2026-09-20)

The local USA ROM (`THE LEGEND OF ZELDA`, 32 MiB, CIC 6105) reaches the opening
horse-riding scene in the host renderer. A bounded host run completes 600
presented frames / 334 display lists with zero presentation or decoder failures.
Rendering is still imperfect; this is boot evidence, not full-game validation.

The follow-up audit completed 1,800 frames / 734 lists with zero decoder or
presentation failures (`oot-menu-audit.log`). The final capture remains at the
title screen despite a synthetic Start at VI 1450, so menu input and gameplay
are not yet validated. The full host suite passed (`oot-audit-suite.log`).

Menu follow-up: **file selection is now verified**. The first title-animation
press reveals the logo; a separate press enters file selection. The host can
now repeat `--start-at VI` (up to 16 distinct pulses, each held for 20 VIs),
and logs the sampled mapped input. Use `--save-slot 1..9` after a bounded run
and `--load-slot 1..9` to resume; pulse times start from the resumed run's VI
counter. Slots use the normal per-ROM save directory and saving replaces the
selected slot. A successful 600-frame checkpoint run with presses at 100 and
300 reached “Please select a file” (`oot-menu2.log`/`.png`), without renderer
failures. This confirms menu entry, not full gameplay.

`GAME_DISC=1 GAME_CHECKPOINT=1` optionally loads a matching `/cd/boot.st` using
the normal ROM identity and integrity checks before running. Missing or invalid
checkpoints fail the diagnostic instead of silently booting. The private local
`not64-oot-menu.cdi` contains the verified file-selection checkpoint; normal
game-disc builds still boot the ROM normally.
Flycast visually confirms File 1–3 / Copy / Erase / Options and starts AICA
after restoring it (`oot-menu-flycast.log`). The Downloads shortcut now opens
this menu snapshot on every launch; use `not64-oot-polished.cdi` for normal
boot and subsequent gameplay saves. No player name/new save was created.

This run exposed and fixed three blockers:
- KOS C++ frame registration resolved `mutex_lock` to a weak no-op in the SDK.
  `platform/dc_kos_compat.c` supplies the real out-of-line KOS lock wrapper;
  the game build now reaches main and starts AICA instead of asserting on unlock.
- Two-cycle texture rectangles were unimplemented. They now use the existing
  two-cycle combiner/blender, with corrected clipped texture coordinates.
- F3DEX2 CULLDL was unimplemented. It now checks loaded homogeneous vertices
  and returns from the current display list, including nested-list callers.

Focused renderer tests cover clipped two-cycle and copy-mode pixels, including
both scissor axes. Culling tests cover nested returns, vertices spanning the
visible volume, a shared outside plane, unloaded vertices and reversed ranges.
Local logs/captures are under `build/dc/validation/oot-*`.

The normal `GAME_DISC=1` defaults remain a bounded 600-frame diagnostic.
For an interactive disc, build with:

```sh
make -f Makefile.dc VIDEO=pvr GFX=soft GAME_DISC=1 GAME_FRAMES=0 GAME_STEPS=0 GAME_START=0
```

Zero disables each diagnostic limit or synthetic Start pulse. The local
`build/dc/not64-oot-polished.cdi` uses that normal-boot configuration; the user's
Downloads `Play Zelda OOT (Dreamcast).cmd` now launches the checkpoint-based
`not64-oot-menu.cdi` described above. The ROM and disc
remain private, ignored build artifacts. When multiple Flycast instances are
open, pass `-ProcessId` to `tools/dc/scrape_console.ps1` to select the intended
serial console. The capture helper checks read/resize results and requires an
output path for reads; a failed or incomplete capture is not reported as success.
Pure-interpreter Dreamcast performance is slow, and full
Ocarina of Time gameplay/hardware compatibility remain unverified.

### Measured faster local build

Latest (2026-09-21): the launcher selects `not64-oot-fast3.cdi`, using the same
flags/checkpoint below. Direct texture-coordinate handling and cached
zero-product RGB equations improve the 60-frame Flycast menu sample from
126.680 to 121.702 seconds (4.1% throughput). Exact host menu/opening captures
and the full suite pass, including coordinate boundaries and combiner alpha.
See `perf3-*.log`. At ~0.49 emulated menu FPS, this is still not playable speed.

The preceding `not64-oot-fast2.cdi` build used the same
flags/checkpoint below. Its 60-frame Flycast menu sample takes 126.680 seconds,
versus 141.779 for the preceding build (11.9% higher throughput). Texture and
blender work reduction preserves exact host menu/opening captures; the full
suite and focused two-cycle/memory-alpha/filter tests pass. See `perf2-*.log`
and `VIDEO_AUDIO_PERF_PLAN.md`. The menu remains far below playable speed.

The preceding `build/dc/not64-oot-fast.cdi` build introduced these flags, retaining the
menu checkpoint and unlimited run. Build with the interactive flags above plus
`GAME_CHECKPOINT=1 PERF=1 LTO=1`. `PERF=1` reports cumulative game-phase
microseconds every 60 presented frames; `LTO=1` enables cross-file optimization
without fast-math. These flags are opt-in and recorded in the compiler stamp.
Renderer optimizations reduce unnecessary depth/blend and texture-sampling work.
Flycast's identical 60-frame menu sample improved from 158.247 to 141.779 seconds
(11.6% throughput gain). The menu and opening host captures remain byte-identical,
and the full optimized regression suite passes. This is still not playable speed.
