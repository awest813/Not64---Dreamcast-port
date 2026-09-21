# Agent handoff — Not64 Dreamcast port

Native `make -f Makefile.dc HOST=1` is rejected at Makefile parse time on LP64
(`sizeof(long) != 4`). Use `make -f Makefile.dc host-test` (Docker armhf + QEMU).
`make -f Makefile.dc` without `KOS_BASE` points at that host path. `GFX=soft`
adds `test-soft` to `test`. `GAME_DISC=1` requires `GFX=soft`. `VIDEO=` / `DEMO=`
are KallistiOS-only.

## Integration checkpoint — 2026-09-20

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
   `count_per_op`/`vilimit` settings levers. Host-side phases need no new
   toolchain; Flycast-measured numbers need the GCC 15.1 SDK first.

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
