#!/usr/bin/env python3
"""Run the CPU fixture through all three cartridge dump byte orders."""
import pathlib
import subprocess
import sys
import tempfile

runner = sys.argv[1:]
source = bytearray(pathlib.Path('roms/dc_cputest.z64').read_bytes())
source[0x3c:0x40] = b'TSEE'
orders = {'z64': (0, 1, 2, 3), 'v64': (1, 0, 3, 2), 'n64': (3, 2, 1, 0)}
with tempfile.TemporaryDirectory() as root:
    for extension, order in orders.items():
        path = pathlib.Path(root) / ('fixture.' + extension)
        path.write_bytes(bytes(source[i+j] for i in range(0,len(source),4) for j in order))
        result = subprocess.run(runner + [str(path)], text=True, capture_output=True)
        if result.returncode or 'CPUTEST PASS' not in result.stdout or 'country=0x45' not in result.stdout:
            sys.exit(f'{extension} FAIL:\n{result.stdout}\n{result.stderr}')
        print(f'ROM byte order + region PASS: {extension}')
