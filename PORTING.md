# Not64 Dreamcast Port — Audit and Plan

**Status:** Phase 2–3 host: interpreter CPUTEST PASS (ADDI/SLTI/BNE + ROM-header decode), Maple→PIF smoke, AI/save smokes. Host stub builds with gcc **or** clang, on Linux and macOS. No KallistiOS ELF yet.  
**Handoff for the next agent:** `AGENT_HANDOFF.md`  
**Repo:** `Not64---Dreamcast-port` (GitHub name is aspirational; the tree is Wii/GC Not64).  
**License:** GPL v2

This document is the working plan. Update the **Current status** section as phases complete.

---

## Executive summary

Not64 is an N64 emulator forked from Wii64/Cube64 (Mupen64-GC). Shipping targets today are **GameCube** and **Wii** (`Makefile.menu2_gc`, `Makefile.menu2_wii`). There is **no** KallistiOS, SH4, PowerVR, or Maple code in the original tree.

A Dreamcast port is a **third-platform bring-up**: reuse the portable emulation core, replace everything tied to PowerPC, libOGC, GX, AESND, and MEM2.

---

## Current status

| Item | Status |
|------|--------|
| Dreamcast / KOS / SH4 code (pre-port) | None in original tree; port files under `platform/`, `Makefile.dc` |
| Porting document | This file |
| Platform types (`platform/dc_types.h`) | Started |
| Dreamcast memory budget (`platform/dc_memory.h`) | Started (paper map) |
| `Makefile.dc` (KOS + host stub) | Host `HOST=1` links the interpreter (gcc or clang, Linux/macOS); KOS still needs `KOS_BASE` |
| Bring-up `main/main_dc.c` | Dummy + CPUTEST, 10000 interpreter steps, host I/O + PIF smokes |
| `fileBrowser-kos` | Started |
| Maple controller (`controller-DC.c`) | Started |
| AICA audio stub (`audio-dc.c`) | Started |
| Interpreter-only core link | **Host verified** (`CPUTEST PASS`) |
| ROM stream (`main/ROM-Cache-dc.c`) | 1 MiB window; z64 words swapped to LE |
| Host I/O smoke | Save file, injected Maple A, AI ring DMA, PIF joybus read/write |
| ROM header decode on DC | Fixed — `dc_fix_header_byte_order()` un-swaps Name/Cartridge_ID/Country_code; asserted by CPUTEST |
| Maple → N64 button map | Done — triggers carry Z/R, `Y`+left trigger is L, both triggers shift the D-pad to the C-buttons; 10 host cases in `smoke_map()` |
| First commercial ROM (host) | **Passes the CIC boot checksum and runs game code**; stops at a TLB store miss, still no VI. See Phase 3.5 |
| Software / PVR renderer | Not started (blocked: no ROM has reached VI yet) |
| Dreamcast menu | Designed only (Phase 8 below); no code. `libgui/` does not port |
| SH4 dynarec | Not started |
| Cloud environment KOS toolchain | **Missing** (`sh-elf-gcc` not installed) |

### Locked Phase 0 decisions (until changed)

| Decision | Choice | Rationale |
|----------|--------|-----------|
| First bootable binary | Diagnostic bring-up, **not** a full ROM runner | Prove KOS video, Maple, and FAT before linking the MIPS core |
| CPU (Phase 1–2) | **Pure interpreter** (`pure_interp.c`), no `PPC_DYNAREC` | PPC JIT cannot be reused; SH4 JIT is a later phase |
| Graphics (first frame) | **Software path later**; bring-up uses KOS bfont/console only | `glN64_GX` is GX-bound; do not start a PVR rewrite until the core links |
| Expansion Pak | **Off** (`USE_EXPANSION` unset) | 4 MB emulated RDRAM is already a large fraction of DC RAM |
| Menu | **None** until core + I/O work | `libgui/` is GX |
| Storage | `/sd/not64/roms` and `/sd/not64/saves` (KOS); POSIX cwd on host stub | Matches existing `not64/` paths |
| Distribution | `.elf` first; `.cdi` later | Need a linking binary before disc packaging |

---

## What this project is

| Attribute | Detail |
|-----------|--------|
| Product | N64 emulator (Not64 / Wii64 fork) |
| Core | Mupen64-derived CPU (`r4300/`), memory/PIF/DMA (`gc_memory/`), RSP HLE (`rsp_hle/`) |
| Graphics (current) | **glN64_GX** — N64 RDP/RSP to Nintendo **GX**; also `GX_gfx/` and `mupen64_soft_gfx/` |
| Input / audio | `gc_input/` (PAD/WPAD), `gc_audio/` (AESND) |
| Abstraction | Compile-time `#ifdef WII` / `NGC` / `__GX__`, not a HAL |
| Branding | Code uses `sd:/not64/`; README still says Wii64 Beta 1.1 |

---

## Portable foundation (reuse)

| Path | Role |
|------|------|
| `r4300/` except `ppc/` | MIPS R4300; start with `pure_interp.c` |
| `gc_memory/` | N64 RAM, TLB, DMA, PIF, saves (MEM2/ARAM are **not** portable) |
| `rsp_hle/` | RSP HLE |
| `fileBrowser/fileBrowser.h` | Function-pointer ROM/save I/O |
| `gc_input/input.c` + `controller_t` | Controller vtable (Maple follows this) |

---

## What must be replaced

| Subsystem | GC/Wii | Dreamcast |
|-----------|--------|-----------|
| Toolchain | `powerpc-eabi-gcc`, libOGC | KallistiOS, `sh-elf-gcc` |
| CPU backend | `r4300/ppc/` MIPS→PPC | Interpreter, then `r4300/sh4/` |
| Graphics | `glN64_GX/` + `__GX__` | Software first, then PVR/KGL |
| Audio | AESND | AICA (`snd_*` / `snd_stream`) |
| Input | PAD / WPAD | Maple |
| Storage | libfat, CARD, DVD, SMB | KOS FAT (`/sd/`, `/cd/`) |
| Menu | `libgui/` GX | Later |
| Memory | `gc_memory/MEM2.h` (~50 MB fixed) | 16 MB total — see `platform/dc_memory.h` |
| Artifact | `.dol` | `.elf` then `.cdi` |

Wii MEM2 (from `gc_memory/MEM2.h`) cannot map 1:1:

- ROM cache 16–192 MB
- TLB LUT 8 MB
- Texture cache 16 MB
- Recompiler metadata 4 MB

Dreamcast has **~16 MB** main RAM.

---

## Architecture notes

There is no unified HAL. Boot, video, and threading live in `main/main_gc-menu2.cpp` (`gccore.h`, `VIDEO_*`, `LWP_*`). GC vs Wii is dual Makefiles plus `-DWII` / `-DNGC`.

**Do not** grow a third `#ifdef` forest without `platform/` headers. Dreamcast files should include `platform/dc_types.h` and `platform/dc_memory.h` instead of `gctypes.h` / `MEM2.h`.

### Existing patterns to copy

- **fileBrowser** function pointers — `fileBrowser-kos.c`
- **controller_t** — `gc_input/controller-DC.c`
- **Makefile variants** — `Makefile.dc` next to `Makefile.menu2_wii`

### Graphics

`glN64_GX` has `__GX__` vs desktop OpenGL/SDL (`#ifndef __GX__`). KOS KGL is not desktop OpenGL. Options later:

| Option | Effort | Notes |
|--------|--------|-------|
| A. Software (`GX_gfx/`, `mupen64_soft_gfx/`) | Medium | Fastest path to a first emulated frame |
| B. Desktop OpenGL → KGL | High | Incomplete match |
| C. New PVR backend | Very high | Best long-term |

---

## Critical blockers

1. **Wrong CPU/OS/SDK** — everything assumes PowerPC + libOGC.
2. **Graphics** — GX is not PowerVR2.
3. **PPC dynarec** — cannot be ported mechanically.
4. **16 MB RAM** — ROM streaming, no 16 MB texture cache, no 8 MB TLB LUT.
5. **No KOS in this Cloud Agent environment** — install toolchain locally or via `environment.json` before hardware/emulator builds.

---

## Phased plan

```
Phase 0  Decisions + environment     (decisions locked; KOS still missing here)
Phase 1  KOS / host bring-up         (done on host stub)
Phase 2  Interpreter core links      (done on host: dummy + CPUTEST)
Phase 3  I/O wired into emulator     (host PIF/Maple/AI/save; AICA on hardware next)
Phase 3.5 First real commercial ROM   (boots past IPL3 into game code; stops at a TLB miss)
Phase 4  First emulated frame        (next: software renderer)
Phase 5  Memory map hardening        (ROM stream, cache sizes)
Phase 6  SH4 dynarec                 (performance)
Phase 7  PVR/KGL renderer            (optional upgrade)
Phase 8  Menu, saves, .cdi           (designed below; 8a ROM browser needs no renderer)
```

### Phase 0 — Pre-work

- [x] Record scope decisions
- [x] Write memory budget (`platform/dc_memory.h`)
- [x] Choose graphics strategy for first frame (software later; console now)
- [ ] Install KallistiOS + `sh-elf-gcc` (not in current cloud image)
- [ ] Define test ROM set (homebrew + 1–2 titles) — dummy + CPUTEST exist; commercial TBD
- [ ] Cloud `environment.json` with KOS when the toolchain is available

### Phase 1 — Bring-up

- [x] `Makefile.dc`
- [x] `main/main_dc.c` — video/console, Maple probe, list `/sd/not64/roms`
- [x] `fileBrowser-kos.c`
- [x] Maple `controller_t`
- [x] AICA-facing audio stub (ring buffer, no DSP yet)
- [ ] Boot the same binary on lxdream/redream or hardware (needs KOS)

### Phase 2 — Interpreter core (host)

- [x] Compile `r4300/pure_interp.c` + `gc_memory/` + `rsp_hle/` with `-D__DREAMCAST__ -DNOASM` and **without** `-DPPC_DYNAREC`
- [x] Guard remaining `gccore.h` / `ogc/` includes on the DC path
- [x] ROM cache that streams (`main/ROM-Cache-dc.c`, 1 MiB)
- [x] Hardcoded ROM path, no menu (`roms/dc_dummy.z64` or argv)
- [x] Host unit run: load dummy header **DC BRINGUP TEST**, execute 10000 steps
- [x] Host CPU test ROM **DC CPUTEST** (ORI/ADDU/XOR/ANDI/SLL/LUI/SW/LW/ADDI/SLTI/BNE + BEQ spin)
- [ ] Same run on a KOS `not64-dc.elf`

### Phase 3 — I/O (host)

- [x] Save dir write/read via `fileBrowser-kos` (`./saves/dc_host.txt`)
- [x] Maple `GetKeys` with host button inject
- [x] Audio plugin ring accepts a fake AI DMA
- [x] Host PIF joybus: status (`0x00`) + buttons (`0x01`) via Maple `getKeys`; `native_ReadController` → `internal_ReadController`
- [ ] Real Maple poll + AICA `snd_stream` on KOS
- [ ] PIF/SI controller stream from a booting commercial/homebrew ROM

Host command:

```sh
make -f Makefile.dc HOST=1 test
# or:
make -f Makefile.dc HOST=1
./not64-dc-bringup                 # roms/dc_cputest.z64
./not64-dc-bringup roms/dc_dummy.z64
```

Expect `CPUTEST PASS`, `map smoke PASS`, `pif smoke PASS` and `Interpreter stopped after step limit 10000`. Builds with gcc or clang, on Linux or macOS.

Dummy ROM: IPL `B -1`. CPUTEST: IPL ALU + store/load to RDRAM `0x80000000` plus ADDI/SLTI/BNE. Generated by `roms/gen_dc_roms.py`.

On a 64-bit host, `address` is `unsigned long`; MIPS `0x80000000` sign-extends. `n64_addr()` truncates before `rwmem[page]`. SH-4 is 32-bit so this is host-only.

Known interpreter glue (not a full decode-recompiler):

- `platform/dc_recomp_stubs.c` — `prefetch_opcode` fills **one** union format per opcode
- `fast_mem_access` treats KSEG0 **and** KSEG1 as unmapped (boot PC `0xa4000040`)
- `dynacore = 2` → `pure_interpreter()`; no `blocks[]` table
- `USE_TLB_CACHE` hash map, not an 8 MiB LUT
- Compact `invalid_code` bit table (GameCube-sized), not Wii MEM2

Do not start SH4 dynarec or PVR until more of a real boot (PIF + RSP) works; CPUTEST is the gate that was blocking that.

### Audit notes (host)

- `n64_addr()` / KSEG1 unmapped fetch are **Dreamcast-only**; Wii/GC `fast_mem_access` is unchanged.
- `r4300/recomp.h` does not include the x86 assembler on `__DREAMCAST__`.
- ROM cache clips reads/writes to `rom_length`; LRU eviction no longer dereferences a NULL window.
- Bring-up calls `cpu_deinit` / `TLBCache_deinit` / `ROMCache_deinit` after a run.
- `make -f Makefile.dc HOST=1 test` is the regression gate.

---

### Phase 3.5 — First real commercial ROM (host stub)

A 32 MiB retail cart (`Mario Golf (USA).z64`) was run through the host stub.
This is the first time anything but a hand-written 15-instruction IPL has gone
through the core, and it exercised paths nothing else had.

```sh
./not64-dc-bringup /path/to/game.z64 50000000     # argv[2] = step budget
```

**Works:**

- [x] Real z64 header: `MarioGolf64`, country `0x45`, PC `0x80025c00` — matches
      the raw bytes, so `dc_fix_header_byte_order()` holds up outside the test ROM
- [x] **`ROMTooBig` streaming path** — 32 MiB ROM through a 1 MiB window. This
      code had never run before; the dummy and CPUTEST images are 4 KiB
- [x] CIC detection from real CRCs (`CIC_Chip=2`)
- [x] IPL3 executes from `SP_DMEM` at `0xa4000040`
- [x] **PI DMA cart -> RDRAM**: `cart=0x10001000 dram=0x00025c00 len=0x100000`,
      and the landing word is `3c08800d` (`LUI r8,0x800d`) — real game code, at
      the address the header's PC points at
- [x] **IPL3's CIC boot checksum passes** (both CRC1 and CRC2) and IPL3 jumps
      to the game — see the LP64 section below for what this took
- [x] 600,000,000 MIPS instructions retired, no `NI` opcode, no unmapped fetch

**Does not work yet — the ROM still never reaches video.** `VI origin` stays 0,
so Phase 4 is not unblocked. But the failure has moved two layers deeper.

#### What was wrong: LP64 bugs in the pure interpreter

The boot dead-ended at `0x800001c8` — `BGEZAL r0,-1`, IPL3's **CIC checksum
failure loop**. It was reached from:

```
800001a4  LUI  r11, 0xb000
800001a8  LW   r8, 0x10(r11)     ; header CRC1
800001ac  BNE  r7, r8, +6        ; -> dead loop
800001b4  LW   r8, 0x14(r11)     ; header CRC2
800001b8  BNE  r16, r8, +3       ; -> dead loop
```

The data was never the problem. Both were verified byte-exact against the raw
file: every `ROMCache_pointer()` read (including across the 1 MiB window
boundary, at 2 MiB and at 16 MiB), and the whole DMA'd 1 MiB in RDRAM
(262,144 words, matching sum, xor, first and last word). **The arithmetic was
wrong.**

`long` is 32 bits on SH4 and PPC32 but 64 on an LP64 host, and the interpreter
uses `long` as "the 32-bit MIPS word":

| Site | Bug on LP64 |
|------|-------------|
| `macros.h` `sign_extended(a) = (long long)((signed long)a)` | No-op — 32-bit results never truncated, so the upper half of a register held garbage |
| `macros.h` `rrt32/rrd32/rrs32/irs32/irt32 = *((long*)…)` | Read/write all 64 bits instead of the low word |
| `pure_interp.c` `SRL`, `SRLV` | `(unsigned long)rrt32` sign-extends a negative operand, then shifts the high `0xFFFFFFFF` down into the result |
| `pure_interp.c` `DIVU` | Same: divides a sign-extended 64-bit value |
| `pure_interp.c` `SLL/SLLV/SRA/SRAV` | Correct by truncation, but made explicitly 32-bit |

All are fixed by using `int`/`unsigned int`, which is 32 bits on SH4, PPC32
**and** LP64 — identical codegen for GC/Wii, correct for the host stub.

**None of these would have failed on real Dreamcast hardware**, where `long`
is 32 bits. They are host-stub fidelity bugs — and the reason the stub could
not run a real ROM. Not64 on Wii never hit them because it runs the PPC
dynarec, not `pure_interp.c`.

Result, in order:

1. Both macros fixes -> CRC1 passes (`r7 = 0x664ba3d4`), CRC2 still fails
2. Plus the shift/divide fixes -> **both checksums pass, IPL3 jumps to the game**
3. 600M instructions later the PC is in game code, then takes a **TLB store
   miss** (`Cause=0x0c`, `EPC=0x800afbe4`) and vectors to `0x80000000`

#### Where it stands now

```
COP0   Count=0x4b052e98 Status=0x00000282 (EXL) Cause=0x0000000c (TLBS) EPC=0x800afbe4
MI     intr=0x0000000a (SI|VI)  mask=0x00000002 (SI)
VI     origin=0 width=0
```

Narrowed further — it is an **infinite TLB refill loop**, not a hang:

- `BadVAddr = 0x00048240`, `EntryHi = 0x00048000` — a KUSEG address, so the
  game legitimately uses the TLB.
- The refill vector is **not** empty. `0x80000000` holds
  `LUI r26,0x800b` / `ADDIU r26,r26,0xfba0` / `JR r26`, so the game's own
  handler is installed and jumps to `0x800afba0`.
- Sampling the PC at 600M / 620M / 700M steps gives `0x80000000`,
  `0x800afbe4`, `0x800afbd4`: it cycles between the vector and the faulting
  store forever. The handler runs, retries the store at `0x800afbe4`, and
  misses on the same address again.

So the TLB entry the handler writes is not taking effect for the subsequent
store. Start at `TLBWR`/`TLBWI` in `r4300/pure_interp.c` (or `cop0.c`) and at
`gc_memory/TLB-Cache-hash.c`, which DC uses via `USE_TLB_CACHE` instead of the
8 MiB LUT and which nothing had exercised before this ROM. Note the DC
`#ifdef`s in that file only stub the zlib savestate dumpers — the lookup path
is unmodified, so suspect the write/invalidate path rather than those guards.
`dump_run_state()` prints `BadVAddr`/`EntryHi`/`Index`/`Wired` to help.

Throughput on this host is ~126M interpreted instructions/sec. An SH4 at
200 MHz will be one to two orders of magnitude slower, which is the
quantitative case for Phase 6.

#### Test coverage added

CPUTEST now runs `SRL/SRLV/SRA/SRAV/SLLV/SUBU/DIVU/MFLO/MFHI` **on a negative
operand** (`0x95F208B7`) — a positive one passes with or without the bugs,
which is why this went unnoticed. It also checks one full 64-bit register
(`r22 = 0xFFFFFFFFF208B700`): the low half looks right even when the high half
is garbage, so only a 64-bit check catches the `macros.h` fault. Each fix was
verified to fail the test when reverted.

`Makefile.dc` now uses `-MMD -MP`. The hand-written dep list omitted
`macros.h`, so editing it silently reused stale objects and produced runs that
disagreed with the source.

---

### Phase 8 — Dreamcast menu (design, no code yet)

The Wii/GC menu is `libgui/` (34 files, ~5.5k lines) + `menu/` (26 files,
~5.2k lines) + `gui/` (~3.6k lines). **None of it ports.** `GraphicsGX.cpp`
is GX; `IPLFont` is the GameCube BIOS font; resources are PowerPC `.s`
blobs; the whole thing is a retained-mode C++ `Component`/`Frame`/
`FocusManager` tree. Phase 8 is a rewrite against KallistiOS, not a port.

#### What the menu is actually for

The contract already exists: `platform/dc_config.c` defines every global the
core reads. The menu's only job is to set those and pick a ROM. Auditing
them against Dreamcast hardware:

| Setting | On Dreamcast |
|---------|--------------|
| `audioEnabled`, `scalePitch` | Keep |
| `showFPSonScreen`, `printToScreen` | Keep |
| `Timers.limitVIs` | Keep |
| `autoLoadSave`, `autoSave`, `saveEnabled` | Keep |
| `padAutoAssign`, `padAssign[4]`, `padType[4]` | Keep — four Maple ports map to four N64 players |
| `pakMode[4]`, `loadButtonSlot` | Keep |
| `screenMode` (4:3 / 16:9) | Keep |
| `skipMenu` | Keep, and **required** — the argv/console bring-up path must survive |
| `nativeSaveDevice` | Rework: `CARDA`/`CARDB` are GameCube memory cards. DC options are SD and **VMU** |
| `saveStateDevice` | Rework: `SD` only until something else exists |
| `videoFormat` / `videoMode` / `videoWidth` | Rework: DC's axis is **VGA vs RGB/composite, NTSC vs PAL**, not Flipper modes |
| `pixelClock`, `trapFilter` | Drop — Flipper video registers |
| `glN64_useFrameBufferTextures`, `glN64_use2xSaiTextures` | Drop — no glN64 on DC |
| `renderCpuFramebuffer` | Drop — GX |
| CPU Emulator (Interpreter / Recompiler) | Hide until `r4300/sh4/` exists (Phase 6) |

Two settings are new and DC-only: **video output mode** (a VGA box cannot do
240p, so some titles need a 480i/VGA choice) and **VMU save layout** — a VMU
holds 128 KiB in 200 blocks, which fits EEPROM (512 B / 2 KiB), SRAM (32 KiB)
and a mempak (32 KiB) comfortably, but a 128 KiB FlashRAM save consumes an
entire card. That sizing decision belongs to a human.

#### Architecture

Immediate-mode, C, under `platform/dc_menu/` — **not** a `libgui/` port. No
class tree, no focus manager, no retained widgets; the Wii menu's framework
buys nothing at four screens.

Draw through one thin surface so the menu is renderer-agnostic:

```
dc_draw_fill_rect(x, y, w, h, rgb565)
dc_draw_text(x, y, rgb565, const char *)
dc_draw_blit(x, y, w, h, const uint16_t *)
```

v1 backs that with KOS `bfont` straight into the framebuffer — the BIOS font
ships in the console, so no font asset and no renderer are needed. Phase 7
can repoint the same three calls at PVR without touching menu logic.

Hard constraints:

- **The menu must free everything before `go()`.** `DC_HEAP_REMAINDER` is
  ~7.8 MiB today, and the Phase 4 software renderer will claim most of it.
- **The menu is optional, never load-bearing.** `skipMenu` plus the existing
  argv path stays the regression harness; `make -f Makefile.dc HOST=1 test`
  must keep passing with no menu compiled in.
- **Input is already done.** The Maple map (triggers carry Z/R, `Y`+left
  trigger is L, both triggers shift the D-pad to the C-buttons) is the menu's
  input source too. Note that a button-config screen has to express the
  *shifts*, which the Wii UI has no concept of — `ConfigureButtonsFrame` maps
  one physical bit to one N64 button and nothing else.
- **Settings file.** Wii writes raw structs to `sd:/not64/settings.cfg`. DC
  should pick a versioned, endian-explicit format at `/sd/not64/settings.cfg`
  rather than inherit that.

#### Shipping order

Each step is independently useful; none blocks the emulator core.

| Step | Scope | Depends on |
|------|-------|-----------|
| **8a** | ROM browser only: list `/sd/not64/roms`, pick, boot. Replaces the argv path. | KOS ELF (Phase 1) + `bfont`. **Not** the software renderer. |
| **8b** | In-game overlay: return to menu, reset, save/load state. | Phase 4 framebuffer; `platform/dc_savestates.c` is still a stub |
| **8c** | Settings, button/shift config, persistence to `/sd/not64/settings.cfg`. | 8a |

8a is the one worth doing early — it is the difference between a demo that
needs a rebuild per ROM and something a person can actually use, and it needs
no renderer.

#### Decisions still owed by a human

1. Video output: auto-detect a VGA box, or expose it as a setting?
2. VMU save layout, and what to do when a FlashRAM title needs a whole card.
3. Does 8a ship before Phase 4? (It can — `bfont` needs no renderer.)

---

## Memory budget (Phase 1 paper map)

See `platform/dc_memory.h`. Conservative target with **4 MB RDRAM**:

| Bucket | Size | Notes |
|--------|------|-------|
| KOS + code + stack | ~3 MB | Not in our heaps |
| Emulated RDRAM | 4 MB | Expansion Pak later |
| ROM stream window | 1 MB | Replace Wii 16 MB+ ROM cache |
| TLB / misc emu | ≤512 KB | No 8 MB LUT |
| Texture cache | 0 for bring-up | 2–4 MB when graphics exists |
| Audio ring | 64 KB | `audio-dc.c` |
| Heap remainder | rest of 16 MB | malloc |

If this does not fit, drop ROM window or RDRAM features before adding a texture cache.

---

## How to build

### Host stub (no KOS) — Phase 1–3

```sh
make -f Makefile.dc HOST=1 test
```

Uses `-DDC_HOST_STUB`. GNU `gcc` required. Runs CPUTEST then the dummy spin ROM.

### KallistiOS (when toolchain is installed)

```sh
source /opt/toolchains/dc/kos/environ.sh   # or your KOS env
make -f Makefile.dc
# produces not64-dc.elf
```

Requires `KOS_BASE`. Load with dcload, or convert to `.cdi` later.

---

## Key files

| Purpose | Path |
|---------|------|
| Next-agent handoff | `AGENT_HANDOFF.md` |
| This plan | `PORTING.md` |
| DC types | `platform/dc_types.h` |
| DC memory map | `platform/dc_memory.h` |
| Bring-up main | `main/main_dc.c` |
| DC Makefile | `Makefile.dc` |
| FAT/POSIX I/O | `fileBrowser/fileBrowser-kos.c` |
| Maple pad | `gc_input/controller-DC.c` |
| Audio stub | `gc_audio/audio-dc.c` |
| ROM load / cache | `main/rom_dc.c`, `main/ROM-Cache-dc.c` |
| Interpreter stubs | `platform/dc_recomp_stubs.c` |
| Dummy / CPUTEST ROMs | `roms/dc_dummy.z64`, `roms/dc_cputest.z64`, `roms/gen_dc_roms.py` |
| Wii boot (reference) | `main/main_gc-menu2.cpp` |
| Wii Makefile (reference) | `Makefile.menu2_wii` |
| CPU interpreter | `r4300/pure_interp.c` |
| PPC JIT (do not use on DC) | `r4300/ppc/` |
| GX renderer (later) | `glN64_GX/` |

---

## Test ROM set (fill in during Phase 2)

| ROM | Why |
|-----|-----|
| `roms/dc_dummy.z64` | Header + IPL spin; host step-limit smoke test |
| `roms/dc_cputest.z64` | Interpreter ALU + RDRAM SW/LW + ADDI/SLTI/BNE |
| Simple 2D commercial (TBD) | First-frame renderer |
| Audio-heavy (TBD) | AICA |

---

## Bottom line

The portable core is real. Host bring-up now **loads a ROM, retires MIPS ALU/LS, talks to the save backend, Maple→PIF, and AI ring**. Next is KallistiOS hardware and a software first frame.
