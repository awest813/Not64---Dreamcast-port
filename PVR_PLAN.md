# Dreamcast PVR audit and implementation plan

Audit date: 2026-09-19. Repository revision: `2998833`.
Scope: source audit, implementation plan, and foundation progress; no renderer performance claim.

## Presenter polish — 2026-09-20

`platform/dc_pvr.c` remains a **VI textured-quad presenter**. It does not
decode RDP or submit game geometry to the TA. `dc_pvr.h` documents that
split; the unused `dc_pvr_available()` stub is gone. Used-row uploads
(`dc_video_pvr_upload_bytes`) replace a full 256 KiB DMA when the frame is
shorter than 256 texels. Shutdown frees the user texture even if the
completion wait fails, then calls `pvr_shutdown()`. Serial `PVR:` /
`PVR timing:` lines are unchanged for `tests/dc/check_target_log.py`.

## Implementation progress — 2026-09-19

Stages 0 and 1 now have host and Flycast evidence: isolated builds, enforced
ILP32 memory contract, checked graphics lifecycle, CPU/PIF/AI/save regressions,
bounded VI conversion, exact image capture, and visible software presentation.
The native PVR presenter also displays the same color bars in Flycast for 120
frames with zero reported failures. Both demos check all 76,800 source pixels.

Target testing exposed and fixed KOS/newlib's `_BIG_ENDIAN` constant being
mistaken for a byte-order predicate in the CPU, memory, and RSP code. Optimized
host regression now also exercises that constant. Software and PVR demos embed
the generated ROM, so they boot without external storage.

Stage 2 now meets its presentation gate in Flycast: eight reopen cycles per
backend, invalid/blank recovery, stable PVR texture allocation, visible final
images, clean final shutdown, and recorded emulated-clock timing.
Main staging and PVR texture each use 256 KiB; PVR reported 6,544,072 free VRAM
bytes after allocation. Physical hardware remains untested.

The opt-in `GFX=soft` path now ports the legacy software rasterizer through a
bounded F3DEX2 task decoder. Mario Golf (USA) boots, renders its opening scene,
and reaches a correctly composed title screen after a Start pulse, visually
verified in both the host capture and the actual Flycast/PVR window.
Texture and palette loading, color saturation, filtering, transparent pixel
rejection, and native controller packet order have focused regressions. This
exercises high-level game graphics with PVR scanout, not raw RDP decoding or
hardware-accelerated geometry. Broader game compatibility, exact coverage/VI
behavior, and full gameplay are not established.
Current commands and limitations are in [tools/dc/README.md](tools/dc/README.md).

The findings and failed macOS build below are the **original audit snapshot**,
not a claim that the now-addressed adapter/build gaps remain unchanged.

## Recommendation

Build a small Dreamcast graphics adapter with a validated CPU framebuffer path,
then a native KallistiOS PVR presenter, then selective hardware acceleration.
Keep the interpreter and 4 MiB RDRAM configuration. Do not port GX calls one by
one or treat the desktop OpenGL branch as a ready-made KGL backend.

The first milestone is a known image generated in emulated RDRAM and displayed
correctly. A PVR textured quad proves presentation, not N64 RDP emulation.
Retain the software-first policy in `AGENT_HANDOFF.md` and `PORTING.md`: the
hardware geometry backend remains gated on an exercised emulated graphics path.
This document elaborates that later work without marking existing phases done.

## Findings, in priority order

| Priority | Finding and source evidence | Consequence / required action |
|---|---|---|
| P1 | `platform/dc_plugins.c:20–36`: `initiateGFX` discards its argument; display-list, raw RDP, VI, and framebuffer callbacks are empty. `Makefile.dc` links no renderer. | There is no PVR implementation to repair. Introduce one graphics adapter with explicit initialization, failure handling, and backend selection. |
| P1 | `main/winlnxdefs.h:39` defines `DWORD` as `unsigned long`; `gc_memory/memory.c:77–85` uses that native-width type for RDRAM and SP storage. | On an LP64 host, nominal 4 MiB RDRAM occupies 8 MiB and the computed IMEM offset becomes 8 KiB instead of 4 KiB. Byte-oriented graphics/RSP access cannot be validated by the current word-zero CPU smoke. Establish 32-bit emulated storage and compatible register interfaces, or use an explicitly validated 32-bit host configuration before claiming host graphics correctness. Do not change shared ABI types blindly. |
| P1 | `gc_memory/memory.c:1608–1610` calls `processRDPList` and immediately raises DP interrupt; other write widths have equivalent paths. `glN64_GX/gDP.cpp:953` also signals DP on FullSync. | A new backend needs one documented owner of DP completion per path. Audit word/byte/halfword/doubleword DPC writes, partial lists, XBUS/DMEM input, and FullSync; avoid premature or duplicated completion. SP task completion is separately handled in `rsp_hle/hle.c:188`. |
| P1 | `mupen64_soft_gfx/main.cpp:248` has an empty raw `ProcessRDPList`; `vi.h:34–43` includes `gccore.h` and a GX texture object; `main.cpp:262` calls `VIDEO_SetPreRetraceCallback` unconditionally. | The software plugin is a source of algorithms, not a portable drop-in or complete correctness oracle. A direct-DPC homebrew test needs a bounded raw decoder; attaching its empty callback will not render it. |
| P1 | `main/main_dc.c:458–459` stops after 10,000 interpreter steps. `roms/gen_dc_roms.py` generates IPL arithmetic/spin tests only. | Existing tests do not prove VI output, graphics task parsing, DP completion, or a sustained frame loop. Add graphics-specific homebrew and callback counters with bounded execution and explicit success criteria. |
| P2 | `glN64_GX/gDP.cpp` calls `OGL_DrawRect`, `OGL_DrawTexturedRect`, and clear functions; `gSP.cpp` calls `OGL_DrawTriangles`; `RSP.cpp` includes OpenGL and architecture-specific matrix paths. `GX_gfx/rsp_GX.cpp` directly emits GX commands. | Reuse selected command decoding, microcode tables, and math only after extracting their dependencies. A backend boundary must capture draw state rather than just rename drawing functions. |
| P2 | `platform/dc_memory.h` budgets main RAM only, sets texture cache to zero, and uses a fixed code/OS allowance. | Add separate measured main-RAM and PVR-RAM budgets, with bounded queues and cache exhaustion behavior. The paper remainder is not measured available heap. |
| P2 | `main/main_dc.c:301` ignores graphics initialization failure; `load_and_step` never calls graphics close callbacks. | Add checked startup and teardown on both success and failure before the backend owns texture memory or pending rendering. |
| P2 | Host and KOS builds in `Makefile.dc` share the same object paths; there are only a few explicit header dependencies and C compilation rules. | Isolate host/SH4 and renderer configurations, generate header dependencies, and add C++ compilation/linking if extracting existing C++ code. Switching targets must not reuse incompatible objects. |

These findings describe the current checkout. PVR rendering quality, throughput,
and game compatibility cannot be measured until a backend exists.

## Validation performed

- Initial tracked working tree was clean.
- Ran `make -f Makefile.dc HOST=1 test`. It failed compiling `r4300/r4300.c:45`:
  `fatal error: 'malloc.h' file not found`. No test executable ran.
- Here, `gcc --version` reports Apple Clang 21 on arm64 macOS. The handoff's
  GNU GCC requirement is not satisfied simply by the Makefile's `CC = gcc`.
- `KOS_BASE` is unset and `sh-elf-gcc` was not found on PATH. No KOS build,
  emulator run, or hardware validation was performed.
- Earlier CPUTEST/PIF passes described in the handoff are historical evidence,
  not a fresh pass on this machine. Fix/reproduce the supported build environment
  as the first implementation step; do not disguise this failure with header stubs.

## Proposed architecture

Keep the public plugin entry points compatible with `main/plugin.h`. Split graphics
symbols out of `platform/dc_plugins.c` while leaving unrelated platform stubs there.
Suggested new files, not existing implementations:

| Module | Responsibility |
|---|---|
| `platform/dc_gfx.c` / `.h` | Own `GFX_INFO`, lifecycle, counters, backend selection, and callback dispatch. Exactly one definition of each plugin symbol. |
| `platform/dc_vi.c` | Read validated VI state and bounded emulated memory; convert RGBA5551/RGBA8888 into a documented output format. Headless image output for tests. |
| `platform/dc_video_kos.c` | Initial software framebuffer presentation and display ownership. |
| `platform/dc_pvr.c` / `.h` | PVR initialization, scene/list submission, used-row texture upload, resource lifetime, and presentation. No N64 command decoding. |
| `platform/dc_gfx/` | Extracted portable state/decoder, bounded draw packets, texture conversion/cache, and a small software reference path. Choose C/C++ consistently with the build. |

There are three independent input paths:

1. CPU-written framebuffer → VI scanout → presenter.
2. DPC command bytes → raw RDP decoder → emulated render target.
3. RSP HLE graphics task → supported microcode decoder → shared draw/state layer.

`rsp_hle/plugin.c:154` enables graphics HLE: it forwards tasks to `processDList`;
it does not turn them into a rasterized image. Raw RDP triangles and high-level
microcode triangles require different frontends. Support both deliberately.

## Implementation sequence and exit gates

### 0. Reproducible builds and memory contract

- Restore a supported GNU toolchain host build or deliberately fix macOS portability;
  pin a KOS/toolchain revision and produce an SH4 ELF with a link map.
- Separate object directories by architecture/backend; use generated dependencies.
- Audit emulated word widths across CPU, memory, plugins, and RSP. Assert RDRAM
  size, DMEM/IMEM offsets, register widths, and shared structure layouts.
- Test CPU writes at multiple adjacent offsets as bytes, halfwords, and words,
  then read them through the graphics memory accessor. Account for word-swapped
  memory: byte offsets and halfword indices use different XOR conventions.

Exit: existing CPU/PIF/AI/save tests pass, cross-component memory tests agree,
and the same bring-up ELF boots on Dreamcast hardware or a named emulator.

### 1. First emulated image and graphics observability

- Add callback/task/VI/DPC counters and explicit unsupported-command diagnostics.
- Add a tiny homebrew ROM that writes color bars and asymmetric corner markers
  to RDRAM and programs VI; retain the existing CPU regression ROMs.
- Implement bounded VI scanout, initially 320×240 RGBA5551. Validate origin,
  stride, visible region, blanking, scale, and source bounds against 4 MiB RDRAM.
  Define unsupported modes explicitly; add RGBA8888 and interlace tests later.
- Keep conversion testable without KOS; capture a deterministic image/checksum.
- Present through the software video path first. Introduce a configurable frame
  or instruction budget so graphics tests can run long enough and still terminate.
- Check initialization failures and close graphics resources before releasing
  emulated memory, including early-return paths.

Exit: homebrew produces the expected image through VI on host and target;
bad origin/stride/blanking inputs do not read outside RDRAM. This proves scanout,
not RDP or RSP rendering.

### 2. PVR presentation only

- Initialize a minimal PVR configuration and render the same converted image on
  one opaque textured quad. Use an explicit padded texture size, initially
  512×256 RGB565 for a 320×240 image, with correct UV limits and border handling.
- Use native KOS APIs. Choose a consistent linear or twiddled layout and matching
  upload/header flags; do not assume the upload call converts pixel formats.
- Start with blocking transfers and simple ownership. Ensure rendering has
  finished using a texture before overwriting or freeing it. Add asynchronous
  uploads only after measuring the baseline.
- Follow scene/list ordering, check failures, and give one component ownership
  of the display; direct console drawing must not corrupt active framebuffers.

Exit: the same color-bar image matches software output, survives repeated frames
and reopen/close cycles, and has recorded memory use and upload/presentation time.

### 3. Small raw RDP reference path

- Implement bounded command parsing with correct command lengths, incomplete
  command retention, source selection, and DPC current/end progression.
- First subset: color-image selection, scissor, fill color/fill rectangles, required
  mode state, and synchronization. Define how unsupported commands stop a test.
- Render this subset in software into emulated RDRAM. Do not depend on the old
  software plugin's empty `ProcessRDPList`.
- Add a ROM issuing these commands and waiting for completion. Resolve DP interrupt
  ownership at the adapter/core boundary; preserve other platforms' behavior.

Exit: rectangles appear through VI, split command lists work, and completion
interrupt tests pass without polling forever or receiving premature completion.

### 4. First accelerated geometry

- Extract one named microcode family selected by an available homebrew fixture
  (for example F3DEX2); reject unknown microcode with diagnostics.
- Route decoded vertices, matrices, viewport, clipping, culling, scissor, depth,
  and primitive color through bounded draw packets to PVR.
- Implement untextured opaque triangles first. Keep raw RDP triangle support a
  separate milestone; an HLE triangle test does not prove raw RDP rasterization.
- Define render-target ownership before accelerating draws. CPU reads, texture
  feedback, VI origin changes, and subsequent software draws must observe the
  correct image; begin with a restricted display-only target if necessary.

Exit: known overlapping/clipped triangle scenes match expected output; target
switches and unsupported cases are detected; geometry budgets cannot overflow.

### 5. Textures and a limited compatibility target

- Start with RGBA16 and simple texture × shade/primitive combinations; add
  IA/I and CI/TLUT conversion according to fixture needs. Test tile addressing,
  TMEM loads, line stride, wrap/mirror/clamp, shifts, and palette changes.
- Cache keys must include image content/version, tile interpretation, palette,
  dimensions, and format. Bound decoded staging bytes and PVR residency separately.
  Pin resources referenced by in-flight scenes; evict only safe entries.
- Add texture rectangles, alpha cutout, then explicitly tested transparency.
  PVR list grouping/sorting must not silently change emulated draw order.
- Classify combiner/blender modes as supported, approximated, or unsupported.
  Do not assume GX TEV or general two-cycle RDP equations map to one PVR pass.
- Keep a deterministic software comparison path for the implemented subset.
  Mixed software/PVR fallback requires a coherent render target; initially fall
  back for an entire supported frame/target, not arbitrary individual primitives.

Exit: textured homebrew passes pixel/scene checks; texture/palette mutation is
visible immediately; cache pressure is bounded; unsupported modes are counted.

### 6. Compatibility and measured optimization

- Add depth/alpha/blend ordering, render-to-texture, CPU framebuffer readback,
  and interlaced/32-bit VI cases one at a time with regression scenes.
- Select a small lawful ROM test set with reproducible checkpoints. Report boot,
  first image, visual correctness, stability, and speed as separate results.
- Measure interpreter, decode, conversion, uploads, PVR submission/wait, and audio
  time. Optimize the measured bottleneck; PVR cannot remove interpreter overhead.
- Consider DMA, batching, texture preconversion, or a later SH4 dynarec only after
  correctness and memory gates hold. No full-speed promise before measurements.

## Memory and lifetime budget

Main RAM and VRAM are separate budgets. Retail Dreamcast PVR memory is 8 MiB
([KOS address constants](https://kos-docs.dreamcast.wiki/group__pvr__addresses.html)).
KOS allocates polygon bins and vertex buffers from the texture-memory pool, so
texture capacity must be measured after initialization
([KOS PVR initialization](https://kos-docs.dreamcast.wiki/pvr_8h_source.html)).

Initial estimates, not allocations already made:

| Allocation | Domain | Size / policy |
|---|---|---|
| Emulated RDRAM | Main RAM | 4 MiB; enforce actual byte size |
| ROM window | Main RAM | Existing 1 MiB |
| Converted 512×256×16-bit staging image | Main RAM | 256 KiB; one initially |
| Draw packets and decoded texture staging | Main RAM | Explicit caps chosen from measured heap headroom |
| Two 640×480×16-bit display buffers | VRAM | 1,228,800 bytes before allocator/layout overhead |
| Presentation texture | VRAM | 256 KiB; 512 KiB if later double-buffered |
| TA vertex buffers, tile bins, overflow space | VRAM | Measure actual KOS configuration; account for buffering |
| Accelerated texture cache | VRAM | Provisional 2 MiB cap only if measured free space leaves safe headroom |

Record code/data/BSS, stack margin, heap high-water usage, VRAM free/largest-block
space, queue overflow, cache eviction, and allocation failures. Exhaustion must
produce a controlled fallback/error, never an unbounded allocation or reuse of
an in-flight texture. Enlarging to a 640×480 source also increases padded texture
storage and must be budgeted explicitly.

## KOS references checked for the design

- [Scene submission](https://kos-docs.dreamcast.wiki/group__pvr__scene__mgmt.html):
  scene lifecycle, grouped polygon lists, and the distinction between readiness
  for another scene and completion of rendering that still references textures.
- [Texture formats](https://kos-docs.dreamcast.wiki/group__pvr__txr__fmts.html) and
  [texture loading](https://kos-docs.dreamcast.wiki/group__pvr__txr__mgmt.html):
  match actual memory layout to texture header flags. Confirm behavior against
  the pinned SDK; avoid relying on conflicting historical loader comments.
- [KOS render-to-texture example](https://kos-docs.dreamcast.wiki/texture__render_8c.html):
  reference for later target ownership work, not evidence this port supports it.

## First implementation change

Start with stage 0 and the graphics adapter/counters from stage 1. Deliver a
reproducible build, verified memory contract, checked graphics lifecycle, and a
CPU-written VI image fixture. Follow with the software presenter and then the
PVR quad. Keep later geometry and compatibility stages in separate reviewable
changes, each with the exit evidence above.
