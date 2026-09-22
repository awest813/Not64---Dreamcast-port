# Controlled replay and Pokémon Stadium 2 trial — 2026-09-21

This implements the software-reference prerequisite from
[the learning plan](DUAL_BUILD_LEARNING_PLAN.md). It does not complete the GPU
capability ladder or establish a new game FPS result.

## Same-source SH4 comparison

Built all three replay binaries from the same working source, based on
`7912c6723833bdcc728e8c4b7a67180773bf0826`, with this follow-up's replay changes.
Used DreamSDK's existing GCC 15.1/KOS environment and isolated Flycast 2.6,
OpenGL, `RenderToTextureBuffer=yes`, `EmulateFramebuffer=no`.

| Build flags | Observed result |
| --- | --- |
| `SOFTWARE_ONLY=1 REFERENCE=1` | All seven fixture groups pass; PVR rasterizer excluded from the link |
| `STRICT=1` | All seven groups pass; exact 96 span and 64 cache case outputs match the software SH4 reference |
| `STRICT=0` | Six fixture groups fail; decoded-cache group passes; checker correctly rejects the run |

Strict GPU scene counts remain 1, 2 and 2 for partial fills, the opaque ramp,
and mixed transitions. This proves those fills executed through PVR. Texture
parity still exercises fallback and must not be treated as evidence of accurate
GPU texture rendering.

The fast negative control reports 65,063 RGB and 38,378 alpha mismatches on
partial fills; 502 ramp mismatches; 69,861 mixed-transition mismatches; and
`0001` instead of the intended red COMBINED consumer in both cold/hot cases.
Its texture-span fixture also fails. Fast mode remains experimental.

Every replay now identifies its backend. The checker rejects a GPU reference,
cross-CPU reference, missing/duplicate identity and incomplete reference output.
Nine acceptance/rejection checks using the captured logs passed. Full ARM32
CPU/platform/software graphics regressions also passed.

Local ignored evidence: `build/dc/validation/dual-{software,strict,fast}.log`,
`dual-manifest.json` (ELF/log SHA-256 and flags), `dual-full-host.log`, and
`check_dual_contract.py`. Build logs use `dual-*-build.log`. No game files are
included in the repository.

## Pokémon Stadium 2: boot trial, not playable

Used the user's local USA `.z64` ROM (64 MiB) with the ARM32 direct interpreter,
software renderer, current 4 MiB RDRAM and isolated native saves. No OOT
checkpoint was loaded. This was a host diagnostic, not a Dreamcast game run.

The 300-million-instruction trial completed its budget with 2,663 VI updates,
1,191 display lists, zero decoder failures, and **zero presented frames**.
2,612 VI updates were unsupported. Capture consequently failed and the process
returned nonzero; this is not a successful boot-to-menu result.

A 30-million-instruction diagnostic rerun completed with 372 VI updates,
153 display lists, 321 unsupported modes, zero presented frames and zero decoder
failures. The first unsupported state was:

```text
status=00013056 origin=00369f80 stride=640
h=006c02ec v=002301fd x=00000400 y=02000800
```

Two concrete blockers were observed:

1. VI status has the interlace bit set; stride and horizontal extent are 640.
   The current converter rejects interlace and supports at most 320×240
   progressive content. Its 512×256 backing texture cannot accommodate this
   width. The fractional Y offset also needs deliberate handling. The first
   logged state is not evidence that every later state was identical.
2. `RS::tri_shade_zbuff` reports cycle type 1 (two-cycle) repeatedly and skips
   those untextured depth-enabled triangles. Zero decoder failures therefore
   does not mean every decoded draw was rendered.

The diagnostic now prints the first unsupported VI register state rather than
only a generic warning. Evidence is in ignored
`build/dc/validation/stadium2-host/{boot,diagnostic}.log`. Both trials finished;
all replay emulator instances from this task were closed.

## Next bounded work

1. Add an analytic two-cycle untextured triangle fixture covering shade/primitive
   combiner selection, alpha and depth, then implement the missing software
   path. Check resulting state as well as pixels. Do not assume routing through
   the textured path is equivalent.
2. Design native 640-wide/interlaced scanout with field parity, fractional crop,
   source bounds and memory budgeting. Add synthetic VI tests before changing
   the converter. Do not ignore the interlace bit, shrink the game's output or
   present stale frames to make the boot counter advance.
3. Repeat the bounded host trial, then build a separate Stadium 2 Dreamcast
   image once visible output is supported. Qualify title/menu before measuring
   FPS, audio or battle compatibility. Expansion-memory requirements remain
   unqualified; do not infer them from this partial boot.
4. Continue the accurate/fast plan with first-divergence reporting and actual
   GPU textured-triangle fixtures plus explicit GPU/fallback counts. Promote
   one proven capability at a time. The last measured strict OOT menu result
   remains 0.586 FPS; this work makes no performance improvement claim.
