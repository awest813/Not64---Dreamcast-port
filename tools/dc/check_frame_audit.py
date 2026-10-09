#!/usr/bin/env python3
"""Decode exact RGB565 serial scanouts; reject incomplete or repeated exports."""
import argparse
from array import array
import json
from pathlib import Path
import re


def read_frame(path):
    lines = Path(path).read_text(encoding="utf-8-sig").splitlines()
    starts = [re.fullmatch(r"Frame audit begin width=(\d+) height=(\d+) pixels=(\d+)", line)
              for line in lines if line.startswith("Frame audit begin")]
    ends = [re.fullmatch(r"Frame audit end runs=(\d+) pixels=(\d+)", line)
            for line in lines if line.startswith("Frame audit end")]
    if len(starts) != 1 or not starts[0] or len(ends) != 1 or not ends[0]:
        raise ValueError("missing, repeated, or malformed frame boundary")
    width, height, total = map(int, starts[0].groups())
    if not (0 < width <= 640 and 0 < height <= 480 and total == width * height):
        raise ValueError("invalid scanout dimensions")
    pixels = array("H")
    runs = 0
    started = finished = False
    for line in lines:
        if line.startswith("Frame audit begin"):
            started = True
        elif line.startswith("Frame audit end"):
            finished = True
        elif line.startswith("Frame runs"):
            match = re.fullmatch(r"Frame runs ([0-9a-f]{6}):((?: [0-9a-f]{10}){1,16})", line)
            if not started or finished or not match or int(match[1], 16) != runs:
                raise ValueError("misordered, missing, repeated, or malformed runs")
            for token in match[2].split():
                count, value = int(token[:6], 16), int(token[6:], 16)
                if not count or len(pixels) + count > total:
                    raise ValueError("zero or overflowing run length")
                pixels.extend(array("H", [value]) * count)
                runs += 1
        elif started and not finished:
            raise ValueError("interrupted or wrapped frame export")
    if tuple(map(int, ends[0].groups())) != (runs, total) or len(pixels) != total:
        raise ValueError("incomplete scanout")
    return width, height, pixels


def rgb(value):
    r, g, b = value >> 11, (value >> 5) & 63, value & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def write_png(path, width, height, pixels):
    from PIL import Image
    image = Image.new("RGB", (width, height))
    image.putdata([rgb(value) for value in pixels])
    image.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--out", type=Path, required=True, help="output filename prefix")
    args = parser.parse_args()
    try:
        width, height, baseline = read_frame(args.baseline)
        cw, ch, candidate = read_frame(args.candidate)
        if (width, height) != (cw, ch):
            raise ValueError("scanout dimensions differ")
        changed = maximum = total_error = 0
        histogram = {}
        difference = []
        for a, b in zip(baseline, candidate):
            delta = tuple(abs(x - y) for x, y in zip(rgb(a), rgb(b)))
            step = max(abs((a >> 11) - (b >> 11)),
                       abs(((a >> 6) & 31) - ((b >> 6) & 31)),
                       abs((a & 31) - (b & 31)))
            changed += a != b
            maximum = max(maximum, step)
            total_error += sum(delta)
            histogram[step] = histogram.get(step, 0) + 1
            difference.append(tuple(min(255, d * 8) for d in delta))
        args.out.parent.mkdir(parents=True, exist_ok=True)
        prefix = str(args.out)
        write_png(prefix + "-baseline.png", width, height, baseline)
        write_png(prefix + "-candidate.png", width, height, candidate)
        from PIL import Image
        image = Image.new("RGB", (width, height))
        image.putdata(difference)
        image.save(prefix + "-difference.png")
        result = dict(width=width, height=height, pixels=len(baseline), changed_pixels=changed,
                      max_five_bit_channel_steps=maximum,
                      mean_rgb_byte_error=total_error / (len(baseline) * 3),
                      five_bit_step_histogram=histogram)
        Path(prefix + ".json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Frame audit FAIL: {error}\n")


if __name__ == "__main__":
    main()
