#!/usr/bin/env python3
"""Check a completed embedded VITEST run's Flycast serial log."""
import re
import sys
from pathlib import Path


def check(path, backend):
    text = Path(path).read_text()
    assert backend in ('software', 'pvr'), 'unknown backend'
    assert 'FAIL' not in text, 'target or emulator reported failure'
    assert text.count('VITEST PASS (76800 CPU-written pixels + VI scanout)') == 9, 'missing pixel checks'
    assert f'Graphics {backend}: VI=121 presented=120 DList=0 RDP=0 invalid=0 unsupported=0 failures=0' in text, 'initial run failed'
    match = re.search(rf'VIDEO STRESS PASS: backend={backend} cycles=8 valid-frames=152 scanout-present-avg-us=(\d+)', text)
    assert match, 'missing lifecycle completion'
    tail = text[match.end():]
    assert 'vid_set_mode:' in tail, 'final display shutdown not observed; wait for the 60-second hold'
    if backend == 'pvr':
        free = re.findall(r'PVR: staging=262144 texture=262144 VRAM-free=(\d+) bytes', text)
        assert len(free) == 9 and len(set(free)) == 1, 'VRAM allocation drift or missing initialization'
        timings = re.findall(r'PVR timing: samples=(\d+) uploads=(\d+) wait-avg-us=(\d+) upload-avg-us=(\d+) submit-avg-us=(\d+)', text)
        assert len(timings) == 9, 'missing shutdown/timing samples'
        assert timings[0][:2] == ('121', '120'), 'initial timing counts'
        assert all(t[:2] == ('22', '19') for t in timings[1:]), 'stress timing counts'
    print(f'Target log PASS: {backend}, 8 reopen cycles, 272 valid frames, scanout/present mean {match[1]} us (emulated clock)')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit('Usage: check_target_log.py software|pvr serial.log')
    try:
        check(sys.argv[2], sys.argv[1])
    except (AssertionError, OSError) as error:
        sys.exit(f'Target log FAIL: {error}')
