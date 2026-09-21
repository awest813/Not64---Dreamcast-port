#!/usr/bin/env python3
"""Fail closed on missing, truncated, repeated, or failing replay serial output."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("log", type=Path)
parser.add_argument("--gpu", action="store_true", help="require actual completed PVR scenes")
parser.add_argument("--reference", type=Path, help="compare texture cases with the unoptimized software replay")
args = parser.parse_args()
lines = args.log.read_text(encoding="utf-8-sig").splitlines()
required = ["partial-fill", "opaque-ramp", "mixed-transition", "combined-cold", "combined-hot", "texture-spans"]
errors = []
cases = [line for line in lines if line.startswith("REPLAY span-case ")]
if len(cases) != 96 or any(not re.fullmatch(rf"REPLAY span-case {n} color=[0-9a-f]{{8}} depth=[0-9a-f]{{8}} tail=[0-9a-f]{{8}} state=[0-9a-f]{{8}}", line) for n, line in enumerate(cases)):
    errors.append("missing, repeated, or malformed texture cases")
if args.reference:
    reference = args.reference.read_text(encoding="utf-8-sig").splitlines()
    expected = [line for line in reference if line.startswith("REPLAY span-case ")]
    reference_summary = [line for line in reference if line.startswith("REPLAY RESULT ")]
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
    for name, minimum in [("partial-fill", 1), ("opaque-ramp", 2), ("mixed-transition", 2)]:
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
