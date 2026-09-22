#!/usr/bin/env python3
"""Fail closed on missing, truncated, repeated, or failing replay serial output."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("log", type=Path)
parser.add_argument("--gpu", action="store_true", help="require actual completed PVR scenes")
parser.add_argument("--software-only", action="store_true", help="require the genuine KOS software-only backend")
parser.add_argument("--reference", type=Path, help="compare texture cases with the unoptimized software replay")
args = parser.parse_args()
lines = args.log.read_text(encoding="utf-8-sig").splitlines()
required = ["partial-fill", "opaque-ramp", "mixed-transition", "combined-cold", "combined-hot", "texture-precision", "texture-spans", "texture-cache"]
errors = []
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
cases = [line for line in lines if line.startswith("REPLAY span-case ")]
if len(cases) != 96 or any(not re.fullmatch(rf"REPLAY span-case {n} color=[0-9a-f]{{8}} depth=[0-9a-f]{{8}} tail=[0-9a-f]{{8}} state=[0-9a-f]{{8}}", line) for n, line in enumerate(cases)):
    errors.append("missing, repeated, or malformed texture cases")
cache_cases = [line for line in lines if line.startswith("REPLAY cache-case ")]
if len(cache_cases) != 64 or any(not re.fullmatch(rf"REPLAY cache-case {n} before=[0-9a-f]{{8}} after=[0-9a-f]{{8}}", line) for n, line in enumerate(cache_cases)):
    errors.append("missing, repeated, or malformed cache cases")
if args.reference:
    reference = args.reference.read_text(encoding="utf-8-sig").splitlines()
    reference_backend = [line for line in reference if line.startswith("REPLAY backend=")]
    expected_backend = "host-software" if backends == ["REPLAY backend=host-software"] else "kos-software"
    if reference_backend != ["REPLAY backend=" + expected_backend]:
        errors.append("reference must use the same CPU target and a software-only backend")
    expected = [line for line in reference if line.startswith("REPLAY span-case ")]
    reference_summary = [line for line in reference if line.startswith("REPLAY RESULT ")]
    if cache_cases != [line for line in reference if line.startswith("REPLAY cache-case ")]:
        errors.append("cache cases differ from the software reference")
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
        gpu_fixtures.append(("texture-precision", 66))
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
