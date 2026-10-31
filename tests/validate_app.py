"""Decode the actual app exporter output independently, not its own report."""
from pathlib import Path
import sys
from PIL import Image

root = Path(sys.argv[1])
cases = {
    'default': (480, 206, 7800, True),
    'long-hold': (480, 206, 13800, True),
    'slow-transition': (480, 206, 9600, True),
    'long-text': (480, 206, 7800, True),
    'single': (480, 206, 2600, True),
    'opaque-color': (480, 206, 7800, False),
}
for name, (w, h, duration, transparent) in cases.items():
    path = root / f'{name}.gif'
    assert 0 < path.stat().st_size < 500_000, (name, 'size cap')
    with Image.open(path) as im:
        assert im.size == (w, h), (name, im.size)
        assert im.info.get('loop') == 0, name
        assert (im.n_frames == 1) if name == 'single' else (im.n_frames > 1), name
        total = 0
        for i in range(im.n_frames):
            im.seek(i)
            total += im.info['duration']
            rgba = im.convert('RGBA')
            assert rgba.getpixel((0, 0))[3] == (0 if transparent else 255), (name, i)
            assert rgba.getpixel((w // 2, h // 2))[3] == 255, (name, i)
        assert total == duration, (name, total, duration)
        print(f'PASS {name}: {path.stat().st_size} bytes, {im.n_frames} frames, {total} ms')
assert (root / 'selftest.txt').read_text().rstrip().endswith('PASS')
