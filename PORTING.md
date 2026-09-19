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
| Software / PVR renderer | Not started |
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
Phase 4  First emulated frame        (next: software renderer)
Phase 5  Memory map hardening        (ROM stream, cache sizes)
Phase 6  SH4 dynarec                 (performance)
Phase 7  PVR/KGL renderer            (optional upgrade)
Phase 8  Menu, saves, .cdi           (polish)
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

Expect `CPUTEST PASS` and `Interpreter stopped after step limit 10000`. Use GNU `gcc`.

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
