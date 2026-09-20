# Agent handoff — Not64 Dreamcast port

## Stopping checkpoint — 2026-09-20

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
| HEAD at handoff | After PIF/Maple host smoke + CPUTEST ADDI/SLTI/BNE (see git log) |
| License | GPL v2 |
| Cloud env | **No KallistiOS / `sh-elf-gcc`.** Host stub only. `cc` is clang and has crashed on `r4300.c`; **use GNU `gcc`.** |

---

## What to run before touching anything

```sh
make -f Makefile.dc HOST=1 test
```

Must print **`CPUTEST PASS`** and **`pif smoke PASS`**, then exit 0. Second process is `roms/dc_dummy.z64` (IPL spin at `0xa4000040`).

```sh
make -f Makefile.dc HOST=1
./not64-dc-bringup                 # default: roms/dc_cputest.z64
./not64-dc-bringup roms/dc_dummy.z64
```

Do **not** use `cc` / clang for this Makefile.

---

## Where the work actually is

The Wii/GC product still builds from `Makefile.menu2_*`. Dreamcast is a **third platform**: interpreter core + new I/O, **not** a PPC dynarec or GX port.

| Path | Role |
|------|------|
| `PORTING.md` | Plan, phase checklist, memory budget |
| `Makefile.dc` | `HOST=1` → `not64-dc-bringup`; else KOS `not64-dc.elf` |
| `main/main_dc.c` | Bring-up: budget, ROM dir, Maple, saves, PIF smoke, load, 10000 interp steps, CPUTEST |
| `main/rom_dc.c` | Header + **LE z64 word swap** (`BYTE_SWAP_HALF` for magic `0x80371240`) |
| `main/ROM-Cache-dc.c` | 1 MiB stream window |
| `platform/dc_*.c`, `dc_types.h`, `dc_memory.h` | Types, budget, plugins/savestate/timer stubs, `prefetch_opcode` |
| `fileBrowser/fileBrowser-kos.c` | POSIX `./roms` `./saves`; KOS `/sd/not64/...` |
| `gc_input/controller-DC.c` | Maple `controller_t`; host inject `controller_DC_host_set` |
| `gc_audio/audio-dc.c` | AI DMA → ring; no AICA yet |
| `roms/gen_dc_roms.py` | Regenerates dummy + CPUTEST `.z64` |
| `roms/dc_cputest.z64` | IPL: ORI/ADDU/XOR/ANDI/SLL/LUI/SW/LW/ADDI/SLTI/BNE + BEQ spin |
| `roms/dc_dummy.z64` | IPL: `B -1` |

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
| Clang ICE on `r4300.c` | Use `gcc` | `Makefile.dc` `CC = gcc` when `HOST=1` |
| Immediate segfault at start | Custom `.sbss` on host ELF | `N64_SBSS` empty on `__DREAMCAST__` |
| Crash in `SLL` / bad `rd` pointer | `prefetch_opcode` wrote every union format | One format per opcode in `platform/dc_recomp_stubs.c` |
| Garbled ROM name then wrong opcodes | LE `*(uint32*)` magic; z64 bytes left BE | Byte-wise magic; `BYTE_SWAP_HALF` for z64 on DC |
| Segfault on `SW` to `0x80000000` on x86_64 | `address` is 64-bit; `0x80000000` sign-extends; `rwmem[address>>16]` explodes | `n64_addr()` in `gc_memory/memory.h` (**DC only**) |
| Boot executes RDRAM not IPL | Wii `fast_mem_access` treats only `0x8/0x9` as unmapped; `0xa4000040` TLB-misses | KSEG0+KSEG1 unmapped **on DC only** — do not change the Wii `#else` |
| `src` undeclared in stubs | Copied `prefetch_opcode` from `recomp.c` (`src`/`dst` live there) | Stubs decode locally; no `src`/`dst` |
| Host `.o` stale after `memory.h` change | Macros inlined into `pure_interp.c` | `Makefile.dc` lists `r4300/pure_interp.o: gc_memory/memory.h` |

Other constraints:

- Interpreter macros in `r4300/macros.h` dereference `PC->f.*`. `PC` must be `malloc`’d (`pure_interpreter`) and `prefetch_opcode` must fill the **right** union arm.
- `go()` with `dynacore == 2` → `pure_interpreter()`. `cpu_init()` sets `interpcore = 0` then DC forces `dynacore = 2` in `main_dc.c`.
- `init_memory()` zeros SP DMEM; `cpu_init()` copies IPL from ROM `0x40` into `SP_DMEM+0x40`. Order in `main_dc.c` is rom_read → init_memory → plugins → `cpu_init` → `go()`.
- Dummy/CPUTEST IPL runs from **`0xa4000040`**, not `ROM_HEADER.PC` (dummy PC is 0).
- `BYTE_SWAP_HALF` in this tree is a **32-bit endian swap**, not 16-bit.
- Do not grow `#ifdef __DREAMCAST__` through `glN64_GX/` until Phase 4.
- Do not “fix” Wii MEM2 / `blocks[]` / PPC paths as part of DC work.

---

## Next work (in order)

1. **KallistiOS ELF** — install `sh-elf-gcc` + KOS (`KOS_BASE`, `environ.sh`). `make -f Makefile.dc` → `not64-dc.elf`. Same bring-up on lxdream/redream or hardware. Cloud image does not have this yet (`environment.json` when someone can install it).
2. **AICA** — `audio-dc.c` only fills a ring. Host smoke is enough; hardware needs `snd_stream` (or equivalent) draining that ring.
3. **Real CPU test / homebrew** — CPUTEST is still a handful of IPL ops, not a full IPL3/PIF/RSP boot. A tiny homebrew `.z64` is the next correctness bar. Host PIF joybus is wired; a booting ROM has not used it yet.
4. **Software first frame** (Phase 4) — only after a ROM actually hits RDP/VI. Start from `mupen64_soft_gfx/` / `GX_gfx/`, not glN64.
5. **SH4 dynarec** — last. New `r4300/sh4/`. PPC JIT is not a template you search-replace.

Skip 4–5 until 1–3 have a ROM that is more than a BEQ spin.

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
