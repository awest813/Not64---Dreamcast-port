#!/usr/bin/env python3
"""Compare post-run SH4 diagnostics from STATE_AUDIT=1 builds.

CPU/event bytes are compared explicitly. Memory uses whole-region and 4 KiB
page CRCs; this is checksum evidence, not a byte-for-byte exported checkpoint.
The derived TLB lookup cache and asynchronous audio ring are not serialized.
"""
import argparse
from pathlib import Path
import re


def read_audit(path, frames):
    text = Path(path).read_text(encoding="utf-8-sig", errors="replace")
    lines = [line.strip() for line in text.splitlines()]
    if lines.count("State audit begin") != 1 or lines.count("State audit end") != 1:
        raise ValueError(f"{path}: missing or repeated audit boundaries")
    start, end = lines.index("State audit begin"), lines.index("State audit end")
    if start >= end:
        raise ValueError(f"{path}: invalid audit order")
    audit = lines[start + 1:end]
    if any(line.startswith("Frame ") for line in audit):
        from check_frame_audit import read_frame
        frame_starts = [i for i, line in enumerate(audit) if line.startswith("Frame audit begin")]
        frame_ends = [i for i, line in enumerate(audit) if line.startswith("Frame audit end")]
        if len(frame_starts) != 1 or len(frame_ends) != 1 or frame_starts[0] >= frame_ends[0]:
            raise ValueError(f"{path}: invalid nested frame boundaries")
        first, last = frame_starts[0], frame_ends[0]
        if any(not line.startswith("Frame runs ") for line in audit[first + 1:last]):
            raise ValueError(f"{path}: interrupted nested frame export")
        read_frame(path)  # Exact run order, dimensions and pixel total required.
        audit = audit[:first] + audit[last + 1:]
    if any(line.startswith("Frame ") for line in audit):
        from check_frame_audit import read_frame
        frame_starts = [i for i, line in enumerate(audit) if line.startswith("Frame audit begin")]
        frame_ends = [i for i, line in enumerate(audit) if line.startswith("Frame audit end")]
        if len(frame_starts) != 1 or len(frame_ends) != 1 or frame_starts[0] >= frame_ends[0]:
            raise ValueError(f"{path}: invalid nested frame boundaries")
        first, last = frame_starts[0], frame_ends[0]
        if any(not line.startswith("Frame runs ") for line in audit[first + 1:last]):
            raise ValueError(f"{path}: interrupted nested frame export")
        read_frame(path)  # Exact run order, dimensions and pixel total required.
        audit = audit[:first] + audit[last + 1:]
    if any(not line.startswith("State ") for line in audit):
        raise ValueError(f"{path}: interrupted or wrapped audit")
    cpu = bytearray()
    pages = []
    names = []
    for line in audit:
        if match := re.fullmatch(r"State bytes ([0-9a-f]{4}):([0-9a-f]+)", line):
            chunk = bytes.fromhex(match[2])
            if int(match[1], 16) != len(cpu) or not 1 <= len(chunk) <= 16:
                raise ValueError(f"{path}: non-contiguous CPU/event bytes")
            cpu.extend(chunk)
        elif match := re.fullmatch(r"State pages ([0-9a-f]{4}):((?: [0-9a-f]{8}){16})", line):
            if int(match[1], 16) != len(pages):
                raise ValueError(f"{path}: non-contiguous RDRAM page hashes")
            pages.extend(match[2].split())
        elif match := re.fullmatch(r"State hash ([\w-]+) (?:bytes=(\d+) )?crc=[0-9a-f]{8}(?: error=(\d+))?", line):
            if match[3] not in (None, "0"):
                raise ValueError(f"{path}: serializer failed")
            if match[1] == "cpu-events" and int(match[2]) != len(cpu):
                raise ValueError(f"{path}: CPU/event size mismatch")
            names.append(match[1])
        elif not re.fullmatch(r"State scanout width=[1-9]\d* height=[1-9]\d* interlaced=[01]", line):
            raise ValueError(f"{path}: unrecognized audit row: {line}")
    required = ["mmio", "cpu-events", "rdram", "sp-dmem", "sp-imem", "pif",
                "flash-info", "eeprom", "sram", "flashram", "mempak", "scanout"]
    if names != required or len(pages) != 1024 or len(cpu) < 700:
        raise ValueError(f"{path}: incomplete state coverage")
    graphics = re.findall(r"Graphics .*: VI=(\d+) presented=\d+ DList=\d+ RDP=\d+ invalid=(\d+) unsupported=(\d+) failures=(\d+)", text)
    if graphics != [(str(frames), "0", "0", "0")]:
        raise ValueError(f"{path}: missing, repeated or invalid graphics completion")
    if re.findall(r"Software graphics: decode-failures=(\d+)", text) != ["0"]:
        raise ValueError(f"{path}: invalid software graphics completion")
    return audit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("--frames", type=int, required=True)
    args = parser.parse_args()
    try:
        baseline = read_audit(args.baseline, args.frames)
        candidate = read_audit(args.candidate, args.frames)
        if baseline != candidate:
            differences = [(a, b) for a, b in zip(baseline, candidate) if a != b]
            for a, b in differences[:12]:
                print(f"baseline:  {a}\ncandidate: {b}")
            raise ValueError(f"state differs ({len(differences)} changed rows, lengths {len(baseline)}/{len(candidate)})")
    except (ValueError, OSError) as error:
        parser.exit(1, f"State validation FAIL: {error}\n")
    print(f"State validation PASS: {args.frames} VI; exact CPU/event bytes, MMIO/memory/page/scanout checksums match")


if __name__ == "__main__":
    main()
