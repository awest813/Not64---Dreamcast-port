# Dreamcast build and graphics foundation

The core currently requires a little-endian ILP32 ABI (32-bit `int`, `long`,
and plugin words). Native 64-bit macOS/Linux builds now fail with an explicit
diagnostic rather than silently giving RDRAM and SP memory the wrong layout.
This is a supported-configuration restriction, not a completed LP64 port.

## Host regression tests on macOS or Linux with Docker

From the repository root:

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
The PVR backend uses one opaque quad and a 512×256 linear RGB565 texture with
blocking uploads. It drains queued TA work, then rendering, before texture reuse
or release. Both temporarily move framebuffer console output to serial and restore
the prior display/logging state on shutdown.

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
