#!/usr/bin/env python3
"""Fail closed on missing, truncated, repeated, or failing replay serial output."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("log", type=Path)
parser.add_argument("--gpu", action="store_true", help="require actual completed PVR scenes")
args = parser.parse_args()
lines = args.log.read_text(encoding="utf-8-sig").splitlines()
required = ["partial-fill", "opaque-ramp", "mixed-transition", "combined-cold", "combined-hot"]
errors = []
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
