# Agent handoff — Not64 Dreamcast port

**Read this first, then `PORTING.md`.** This file is for the next agent, not a design essay.

| | |
|--|--|
| Repo | `awest813/Not64---Dreamcast-port` (tree is Wii/GC Not64) |
| Branch | `cursor/dreamcast-port-plan-fc3a` |
| Base | `master` |
| PR | https://github.com/awest813/Not64---Dreamcast-port/pull/1 (draft) |
| HEAD at handoff | After PIF/Maple host smoke + CPUTEST ADDI/SLTI/BNE (see git log) |
| License | GPL v2 |
| Cloud env | **No KallistiOS / `sh-elf-gcc`.** Host stub only. The stub now builds with gcc **or** clang, on Linux and macOS; `Makefile.dc` still defaults to `gcc` because an older clang ICE'd on `r4300.c`. Override with `make -f Makefile.dc HOST=1 CC=clang`. |

---

## What to run before touching anything

```sh
make -f Makefile.dc HOST=1 test
```

Must print **`CPUTEST PASS`** and **`pif smoke PASS`**, then exit 0. Second process is `roms/dc_dummy.z64` (IPL spin at `0xa4000040`).

```sh
make -f Makefile.dc HOST=1
./not64-dc-bringup                          # default: roms/dc_cputest.z64
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
| `gc_audio/audio-dc.c` | AI DMA → ring; no AICA yet |
| `roms/gen_dc_roms.py` | Regenerates dummy + CPUTEST `.z64` |
| `roms/dc_cputest.z64` | IPL: ORI/ADDU/XOR/ANDI/SLL/LUI/SW/LW/ADDI/SLTI/BNE + BEQ spin; header carries `'DO'` / `'E'` / v1 to exercise the DC header un-swap |
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
8. **Controller map** — triggers carry Z/R, `Y`+left trigger is L, both triggers shift the D-pad to the C-buttons. See **Controller mapping** below.
9. **Second audit polish** — host stub builds under clang/macOS (`gc_input/input.c` nested functions removed, `<malloc.h>` guarded, `invalidate_func` declared); DC ROM-header byte order fixed and asserted; PIF joybus store made alignment- and LP64-safe; ROM cache bounds/LRU/NULL fixes; AI DMA clamped to RDRAM; `PC` no longer leaked per run; recursive `mkdir` for `/sd/not64/...`; `get_savespath()` correct on KOS.

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
| Host `.o` stale after `memory.h` change | Macros inlined into `pure_interp.c` | `Makefile.dc` lists `r4300/pure_interp.o: gc_memory/memory.h` |
| `error: function definition is not allowed here` in `input.c` | GCC nested functions in `load_configurations` | Replaced with `read_config_pointer()` + local macros; call sites unchanged |
| `malloc.h: file not found` on macOS | Darwin/BSD have no `<malloc.h>` | Guarded in `r4300/r4300.c` and `gc_memory/dma.c` |
| `call to undeclared function 'invalidate_func'` | Only declared by `r4300/ppc/Wrappers.h` | Declared in `dma.c` under `__DREAMCAST__` |
| Garbage `Cartridge_ID` / `Country_code` / region | ROM words stored byte-reversed, so the header's byte/halfword fields scramble while its 32-bit fields stay correct | `dc_fix_header_byte_order()` in `rom_dc.c`; CPUTEST asserts `'DO'`/`0x45`/`isEEPROM16k` |
| CPUTEST "passes" after a loader regression | Checks were gated on `goodname == "DC CPUTEST"`, which a broken loader never produces | `check_cputest(path)` fails when the CPUTEST image was requested but did not decode |
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
| **Y + left trigger** | **L** |
| **Both triggers + D-pad** | **C-Up / C-Down / C-Left / C-Right** |
| X | unassigned |

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

`smoke_map()` in `main/main_dc.c` covers all ten branches on the host stub.

---

## Open findings (audited, deliberately not changed)

1. **Analog range is unscaled.** `_GetKeys` converts Maple 0–255 straight to ±127; a real N64 stick saturates near ±80, so games will read as over-deflected. `controller-GC.c` scales; DC does not yet.
2. **`fileBrowser_kos_readFile` does `fopen`/`fseek`/`fclose` per call.** The ROM cache streams in 64 KiB blocks, so every page-in reopens the file. Fine on a host filesystem, likely unacceptable on Dreamcast SD/GD — cache the handle before Phase 3 performance work.
3. **Host stub is not an SH4 model.** `unsigned long` is 64-bit on an LP64 host and 32-bit on SH4, so `rdram[]`, `reg[]` and every `read_*_in_memory()` differ in width and layout. `CPUTEST PASS` on the host is a link/logic check, not evidence about hardware. The alignment and LP64 bugs found in `pif.c` are exactly the class the host stub cannot catch by itself.

---

## Next work (in order)

1. **KallistiOS ELF** — install `sh-elf-gcc` + KOS (`KOS_BASE`, `environ.sh`). `make -f Makefile.dc` → `not64-dc.elf`. Same bring-up on lxdream/redream or hardware. Cloud image does not have this yet (`environment.json` when someone can install it).
2. **AICA** — `audio-dc.c` only fills a ring. Host smoke is enough; hardware needs `snd_stream` (or equivalent) draining that ring.
3. **Get a real ROM to VI** — *this is the live problem.* A retail 32 MiB cart now loads, runs IPL3, PI-DMAs the game into RDRAM at its header PC, and retires **50M instructions with no exception, no NI and no unmapped fetch** — then loops in real code at `0x80000130`-`0x80000188` and never sets `VI origin`. Full evidence, including what is ruled out, is **Phase 3.5 in `PORTING.md`**. Start there: `EPC` is untouched so it is not an exception loop, and the PI DMA is verified correct, so suspect the boot handshake the loop is polling (`PIF_RAM[0x3C]` is all zeros where hardware leaves a CIC/PIF value).
4. **Menu step 8a** — ROM browser over `/sd/not64/roms` on KOS `bfont`. Needs no renderer, so it can land right after the KOS ELF and replaces the argv path. Design (screens, which `dc_config.c` settings survive on DC, why `libgui/` does not port) is **Phase 8 in `PORTING.md`** — read it before writing menu code.
5. **Software first frame** (Phase 4) — only after a ROM actually hits RDP/VI. Start from `mupen64_soft_gfx/` / `GX_gfx/`, not glN64.
6. **SH4 dynarec** — last. New `r4300/sh4/`. PPC JIT is not a template you search-replace.

Skip 5–6 until 1–3 have a ROM that is more than a BEQ spin. 8a (step 4) is independent of all of them once the KOS ELF exists.

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

> Continue the Not64 Dreamcast port from `AGENT_HANDOFF.md`. Run `make -f Makefile.dc HOST=1 test` first. Do not start PVR or SH4 dynarec. If KOS is available, produce `not64-dc.elf`; otherwise add a tiny homebrew `.z64` past IPL, or AICA drain. Software renderer only after a ROM hits RDP/VI.

Update **this file** and `PORTING.md` current-status when a phase actually finishes.
