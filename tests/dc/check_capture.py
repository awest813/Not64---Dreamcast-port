"""Check every captured pixel against the ROM's independent RGB color pattern."""
from pathlib import Path
import hashlib
import sys

data = Path(sys.argv[1]).read_bytes()
header = b"P6\n320 240\n255\n"
assert data.startswith(header), "bad PPM format/dimensions"
rgb = data[len(header):]
assert len(rgb) == 320 * 240 * 3, "bad pixel count"
colors = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
          (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
corners = {(0, 0): (255, 0, 0), (319, 0): (0, 255, 0),
           (0, 239): (0, 0, 255), (319, 239): (255, 255, 255)}
for y in range(240):
    for x in range(320):
        offset = (y * 320 + x) * 3
        expected = corners.get((x, y), colors[x // 40])
        assert tuple(rgb[offset:offset + 3]) == expected, f"pixel mismatch at {x},{y}"
print(f"VI capture PASS: 76800 pixels, SHA256={hashlib.sha256(data).hexdigest()}")
