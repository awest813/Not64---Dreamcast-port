#!/usr/bin/env python3
"""Fail closed on missing, truncated, repeated, or failing replay serial output."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("log", type=Path)
parser.add_argument("--gpu", action="store_true", help="require actual completed PVR scenes")
parser.add_argument("--software-only", action="store_true", help="require the genuine KOS software-only backend")
parser.add_argument("--wide", action="store_true", help="require native 640-pixel canvas and stride transitions")
parser.add_argument("--affine", action="store_true", help="require the experimental varying-shade GPU probe")
parser.add_argument("--batch-cache", action="store_true", help="require both cache eviction/retention GPU probes")
parser.add_argument("--cache-index", action="store_true", help="require the hash-collision GPU probe")
parser.add_argument("--identity", action="store_true", help="require the opt-in identity-blender GPU probe")
parser.add_argument("--full-alpha", action="store_true", help="require the constant-alpha coverage GPU probe")
parser.add_argument("--texture-alpha", action="store_true", help="require uniform source-alpha coverage GPU work")
parser.add_argument("--cvg-point", action="store_true", help="require integer coverage-mask GPU work")
parser.add_argument("--identity-span", action="store_true", help="require the software identity-span build and scalar comparison")
parser.add_argument("--combine-span", action="store_true", help="require the exact simple-combiner software span")
parser.add_argument("--rect-sample", action="store_true", help="require filtered rectangle pixel-center GPU work")
parser.add_argument("--shade-scale", action="store_true", help="require corrected shaded packed-color precision")
parser.add_argument("--tri-origin", action="store_true", help="require triangle sample correction without changing coverage")
parser.add_argument("--filter-grid", action="store_true", help="require bounded quarter-texel filtered triangle grids")
parser.add_argument("--rect-clip", action="store_true", help="require the clipped rectangle GPU probe")
parser.add_argument("--cutout", action="store_true", help="require experimental texture-alpha shade GPU work")
parser.add_argument("--reference", type=Path, help="compare texture cases with the unoptimized software replay")
args = parser.parse_args()
lines = args.log.read_text(encoding="utf-8-sig").splitlines()
required = ["framebuffer-import", "partial-fill", "opaque-ramp", "mixed-transition", "fill-texture-transition", "combined-cold", "combined-hot", "unused-tile", "affine-shade", "texture-precision", "texture-spans", "second-texture", "texture-cache", "texture-mutations", "identity-blender", "cutout-shade", "texture-dependencies", "texture-batches", "full-alpha", "rectangle-scissor", "rectangle-sample"]
required.append("texture-hash-collision")
required.append("texture-alpha")
required.append("coverage-point")
required.append("software-identity-span")
required.append("software-combine-values")
required.append("shaded-precision")
required.append("triangle-origin")
required.append("triangle-filter")
if args.wide: required.append("wide-native")
errors = []
filter_enabled=[line for line in lines if line.startswith("REPLAY filter-grid-enabled=")]
if filter_enabled not in (["REPLAY filter-grid-enabled=0"],["REPLAY filter-grid-enabled=1"]):
    errors.append("missing or repeated filter-grid build identity")
if args.filter_grid and filter_enabled != ["REPLAY filter-grid-enabled=1"]:
    errors.append("filter-grid correction was not enabled")
filter_probes=[re.fullmatch(r"REPLAY triangle-filter cases=8 guards=4 baseline-error=(\d+) corrected-error=(\d+) max-channel-error=(\d+) mismatches=0 PASS",line)
               for line in lines if line.startswith("REPLAY triangle-filter ") and "completed-scenes=" not in line]
if len(filter_probes)!=1 or not filter_probes[0]:
    errors.append("missing or failing complete filtered-triangle probes")
elif args.filter_grid and (int(filter_probes[0][3])>5 or int(filter_probes[0][2])*2>int(filter_probes[0][1])):
    errors.append("filtered triangle accuracy bound or improvement failed")
filter_cases=[line for line in lines if line.startswith("REPLAY triangle-filter-case ")]
filter_matches=[re.fullmatch(rf"REPLAY triangle-filter-case {n} baseline-error=(\d+) corrected-error=(\d+) max-channel-error=(\d+)",line)
                for n,line in enumerate(filter_cases)]
if len(filter_matches)!=12 or not all(filter_matches):
    errors.append("missing, repeated or malformed triangle-filter case sequence")
elif any(match.groups()!=("0","0","0") for match in filter_matches[8:]):
    errors.append("filtered triangle guard controls changed output")
if args.filter_grid:
    if not (args.gpu and args.affine and args.shade_scale and args.tri_origin):
        errors.append("filter-grid requires the qualified GPU/shade/triangle-origin build")
    if [line for line in lines if line.startswith("REPLAY triangle-filter grids=")] != ["REPLAY triangle-filter grids=8 completed-scenes=48"]:
        errors.append("filtered triangle GPU grids or scenes were not established")
origin_enabled=[line for line in lines if line.startswith("REPLAY tri-origin-enabled=")]
if origin_enabled not in (["REPLAY tri-origin-enabled=0"],["REPLAY tri-origin-enabled=1"]):
    errors.append("missing or repeated triangle-origin build identity")
if args.tri_origin and origin_enabled != ["REPLAY tri-origin-enabled=1"]:
    errors.append("triangle-origin correction was not enabled")
origin_probes=[line for line in lines if line.startswith("REPLAY triangle-origin ") and "completed-scenes=" not in line]
if origin_probes != ["REPLAY triangle-origin projection-cases=40 footprint-cases=4 sample-cases=4 mismatches=0 PASS"]:
    errors.append("missing or failing complete triangle-origin projection/footprint/sample checks")
if args.tri_origin and [line for line in lines if line.startswith("REPLAY triangle-origin corrected=")] != ["REPLAY triangle-origin corrected=7 completed-scenes=12"]:
    errors.append("triangle-origin probes did not execute the required GPU corrections")
span_enabled=[line for line in lines if line.startswith("REPLAY identity-span-enabled=")]
if span_enabled not in (["REPLAY identity-span-enabled=0"],["REPLAY identity-span-enabled=1"]):
    errors.append("missing or repeated software identity-span build identity")
if args.identity_span and span_enabled != ["REPLAY identity-span-enabled=1"]:
    errors.append("software identity-span optimization was not enabled")
if [line for line in lines if line.startswith("REPLAY software-identity-span ")] != ["REPLAY software-identity-span mismatches=0 cases=320 PASS"]:
    errors.append("missing, repeated or failing 320-case software identity/combiner-span comparison")
combine_enabled=[line for line in lines if line.startswith("REPLAY combine-span-enabled=")]
if combine_enabled not in (["REPLAY combine-span-enabled=0"],["REPLAY combine-span-enabled=1"]):
    errors.append("missing or repeated simple-combiner span build identity")
if args.combine_span and combine_enabled != ["REPLAY combine-span-enabled=1"]:
    errors.append("simple-combiner span optimization was not enabled")
combine_cases=[line for line in lines if line.startswith("REPLAY software-combine-values ")]
expected_optimized=4096 if combine_enabled==["REPLAY combine-span-enabled=1"] else 0
if combine_cases != [f"REPLAY software-combine-values mismatches=0 cases=4096 optimized={expected_optimized} PASS"]:
    errors.append("missing, repeated or failing combiner value/state controls")
shade_enabled=[line for line in lines if line.startswith("REPLAY shade-scale-enabled=")]
if shade_enabled not in (["REPLAY shade-scale-enabled=0"],["REPLAY shade-scale-enabled=1"]):
    errors.append("missing or repeated shaded-color build identity")
if args.shade_scale and shade_enabled != ["REPLAY shade-scale-enabled=1"]:
    errors.append("shaded packed-color correction was not enabled")
shaded = [re.fullmatch(r"REPLAY shaded-precision cases=128 channel-error=\d+ max-channel-error=(\d+) mismatches=0 PASS", line)
          for line in lines if line.startswith("REPLAY shaded-precision ") and "completed-scenes=" not in line]
if len(shaded)!=1 or not shaded[0] or (args.shade_scale and int(shaded[0][1])>1):
    errors.append("missing or failing complete shaded-color precision probes")
backends = [line for line in lines if line.startswith("REPLAY backend=")]
if len(backends)!=1 or backends[0] not in ["REPLAY backend="+name for name in
        ("host-software","kos-software","kos-pvr-strict","kos-pvr-fast")]:
    errors.append("missing, repeated, or unknown backend identity")
if args.software_only and (backends != ["REPLAY backend=kos-software"] or
        any("completed-scenes=" in line for line in lines)):
    errors.append("software-only KOS execution was not established")
if args.gpu and (args.software_only or not backends or backends[0] not in
        ("REPLAY backend=kos-pvr-strict","REPLAY backend=kos-pvr-fast")):
    errors.append("GPU backend identity was not established")
if args.affine:
    scenes = [re.fullmatch(r"REPLAY affine-shade completed-scenes=(\d+)", line) for line in lines]
    scenes = [int(match[1]) for match in scenes if match]
    if backends != ["REPLAY backend=kos-pvr-fast"] or len(scenes)!=1 or scenes[0]<4:
        errors.append("experimental varying-shade GPU execution was not established")
if args.cutout and (not args.affine or backends != ["REPLAY backend=kos-pvr-fast"]):
    errors.append("cutout GPU probe requires the fast varying-shade backend")
affine_cases = [line for line in lines if line.startswith("REPLAY affine-case ")]
if len(affine_cases) != 6 or any(not re.fullmatch(rf"REPLAY affine-case {n} max-channel-error=\d+", line) for n, line in enumerate(affine_cases)):
    errors.append("missing, repeated, or malformed varying-shade cases")
cutout_cases = [line for line in lines if line.startswith("REPLAY cutout-case ")]
if len(cutout_cases) != 5 or any(not re.fullmatch(rf"REPLAY cutout-case {n} max-channel-error=\d+", line) for n, line in enumerate(cutout_cases)):
    errors.append("missing, repeated, or malformed cutout-shade cases")
full_alpha_cases = [line for line in lines if line.startswith("REPLAY full-alpha-case ")]
if len(full_alpha_cases)!=8 or any(not re.fullmatch(rf"REPLAY full-alpha-case {n} expected=[0-9a-f]{{4}}",line) for n,line in enumerate(full_alpha_cases)):
    errors.append("missing, repeated, or malformed full-alpha cases")
if (args.full_alpha or args.rect_clip or args.rect_sample or args.shade_scale or args.tri_origin or args.identity or args.batch_cache or args.cache_index) and (not args.gpu or backends != ["REPLAY backend=kos-pvr-fast"]):
    errors.append("requested fast GPU feature requires the fast GPU backend")
texture_alpha_cases = [line for line in lines if line.startswith("REPLAY texture-alpha-case ")]
coverage_cases = [line for line in lines if line.startswith("REPLAY coverage-point-case ")]
if len(coverage_cases)!=15 or any(not re.fullmatch(rf"REPLAY coverage-point-case {n} alpha=\d+ hash=[0-9a-f]{{8}}",line) for n,line in enumerate(coverage_cases)):
    errors.append("missing, repeated, or malformed integer coverage cases")
if args.cvg_point and (not args.full_alpha or not args.gpu or backends != ["REPLAY backend=kos-pvr-fast"]):
    errors.append("integer coverage proof requires full-alpha fast GPU mode")
if len(texture_alpha_cases)!=9 or any(not re.fullmatch(rf"REPLAY texture-alpha-case {n} expected=[0-9a-f]{{4}}",line) for n,line in enumerate(texture_alpha_cases)):
    errors.append("missing, repeated, or malformed texture-alpha cases")
if args.texture_alpha and (not args.full_alpha or not args.gpu or backends != ["REPLAY backend=kos-pvr-fast"]):
    errors.append("texture-alpha requires the fast full-alpha GPU backend")
cases = [line for line in lines if line.startswith("REPLAY span-case ")]
if len(cases) != 96 or any(not re.fullmatch(rf"REPLAY span-case {n} color=[0-9a-f]{{8}} depth=[0-9a-f]{{8}} tail=[0-9a-f]{{8}} state=[0-9a-f]{{8}}", line) for n, line in enumerate(cases)):
    errors.append("missing, repeated, or malformed texture cases")
cache_cases = [line for line in lines if line.startswith("REPLAY cache-case ")]
if len(cache_cases) != 64 or any(not re.fullmatch(rf"REPLAY cache-case {n} before=[0-9a-f]{{8}} after=[0-9a-f]{{8}}", line) for n, line in enumerate(cache_cases)):
    errors.append("missing, repeated, or malformed cache cases")
pair_cases = [line for line in lines if line.startswith("REPLAY pair-case ")]
if len(pair_cases) != 64 or any(not re.fullmatch(rf"REPLAY pair-case {n} color=[0-9a-f]{{8}} depth=[0-9a-f]{{8}} before=[0-9a-f]{{8}} state=[0-9a-f]{{8}}", line) for n, line in enumerate(pair_cases)):
    errors.append("missing, repeated, or malformed second-texture cases")
mutation_cases = [line for line in lines if line.startswith("REPLAY mutation-case ")]
if len(mutation_cases)!=15 or any(not re.fullmatch(rf"REPLAY mutation-case {n} actual=[0-9a-f]{{4}} expected=[0-9a-f]{{4}}",line) for n,line in enumerate(mutation_cases)):
    errors.append("missing, repeated, or malformed texture mutation cases")
dependency_cases = [line for line in lines if line.startswith("REPLAY dependency-case ")]
if len(dependency_cases)!=12 or any(not re.fullmatch(rf"REPLAY dependency-case {n} actual=[0-9a-f]{{4}} expected=[0-9a-f]{{4}}",line) for n,line in enumerate(dependency_cases)):
    errors.append("missing, repeated, or malformed texture dependency cases")
collision_cases = [line for line in lines if line.startswith("REPLAY collision-case ")]
collision_matches = [re.fullmatch(rf"REPLAY collision-case {n} hash=([0-9a-f]{{8}}) actual=([0-9a-f]{{4}}) expected=([0-9a-f]{{4}})", line)
                     for n, line in enumerate(collision_cases)]
if len(collision_matches) != 4 or not all(collision_matches):
    errors.append("missing, repeated, or malformed hash collision cases")
elif len({match[1] for match in collision_matches}) != 1 or any(match[2] != match[3] for match in collision_matches):
    errors.append("hash collision proof or texture output failed")
if args.reference:

    reference = args.reference.read_text(encoding="utf-8-sig").splitlines()
    if coverage_cases != [line for line in reference if line.startswith("REPLAY coverage-point-case ")]:
        errors.append("integer coverage cases differ from the software reference")
    if texture_alpha_cases != [line for line in reference if line.startswith("REPLAY texture-alpha-case ")]:
        errors.append("texture-alpha cases differ from the software reference")
    if collision_cases != [line for line in reference if line.startswith("REPLAY collision-case ")]:
        errors.append("hash collision cases differ from the software reference")
    reference_backend = [line for line in reference if line.startswith("REPLAY backend=")]
    expected_backend = "host-software" if backends == ["REPLAY backend=host-software"] else "kos-software"
    if reference_backend != ["REPLAY backend=" + expected_backend]:
        errors.append("reference must use the same CPU target and a software-only backend")
    if dependency_cases != [line for line in reference if line.startswith("REPLAY dependency-case ")]:
        errors.append("texture dependency cases differ from the software reference")
    if mutation_cases != [line for line in reference if line.startswith("REPLAY mutation-case ")]:
        errors.append("texture mutation cases differ from the software reference")
    expected = [line for line in reference if line.startswith("REPLAY span-case ")]
    reference_summary = [line for line in reference if line.startswith("REPLAY RESULT ")]
    if cache_cases != [line for line in reference if line.startswith("REPLAY cache-case ")]:
        errors.append("cache cases differ from the software reference")
    if pair_cases != [line for line in reference if line.startswith("REPLAY pair-case ")]:
        errors.append("second-texture cases differ from the software reference")
    if cases != expected or reference_summary != ["REPLAY RESULT failures=0 stop=0 PASS"]:
        errors.append("texture cases differ from the completed software reference")
    for name in required:
        results = [line for line in reference if line.startswith(f"REPLAY {name} ") and "completed-scenes=" not in line]
        if len(results) != 1 or not results[0].endswith(" PASS"):
            errors.append(f"reference {name}: missing, repeated, or failed result")
for name in required:
    results = [line for line in lines if line.startswith(f"REPLAY {name} ") and "completed-scenes=" not in line]
    if len(results) != 1 or not results[0].endswith(" PASS"):
        errors.append(f"{name}: missing, repeated, or failed result")
if args.gpu:
    gpu_fixtures = [("partial-fill", 1), ("opaque-ramp", 2), ("mixed-transition", 2)]
    if backends == ["REPLAY backend=kos-pvr-fast"]:
        if args.batch_cache: gpu_fixtures.append(("texture-batches",4))
        if args.cache_index: gpu_fixtures.append(("texture-hash-collision",4))
        if args.full_alpha: gpu_fixtures.append(("full-alpha",4))
        if args.texture_alpha: gpu_fixtures.append(("texture-alpha",4))
        if args.cvg_point: gpu_fixtures.append(("coverage-point",11))
        if args.rect_sample: gpu_fixtures.append(("rectangle-sample",2))
        if args.shade_scale: gpu_fixtures.append(("shaded-precision",256 if args.cutout else 192))
        if args.rect_clip: gpu_fixtures.append(("rectangle-scissor",3))
        if args.identity: gpu_fixtures.append(("identity-blender",2))
        if args.cutout: gpu_fixtures.append(("cutout-shade",6))
        gpu_fixtures.append(("texture-mutations", 5))
        gpu_fixtures.append(("unused-tile", 1))
        gpu_fixtures.append(("texture-precision", 66))
        gpu_fixtures.append(("fill-texture-transition", 5))
    else:
        gpu_fixtures.append(("fill-texture-transition", 3))
    if args.wide: gpu_fixtures.append(("wide-native",8 if backends == ["REPLAY backend=kos-pvr-fast"] else 3))
    for name, minimum in gpu_fixtures:
        scenes = [re.fullmatch(rf"REPLAY {name} completed-scenes=(\d+)", line) for line in lines]
        scenes = [int(match[1]) for match in scenes if match]
        if len(scenes) != 1 or scenes[0] < minimum:
            errors.append(f"{name}: required GPU work was not established")
summary = [line for line in lines if line.startswith("REPLAY RESULT ")]
if summary != ["REPLAY RESULT failures=0 stop=0 PASS"]:
    errors.append("missing, repeated, or failing completion summary")
if errors:
    raise SystemExit("Replay validation FAIL: " + "; ".join(errors))
print("Replay validation PASS" + (" (GPU exercised)" if args.gpu else ""))
