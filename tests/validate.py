from pathlib import Path
from PIL import Image

count = 0
for row in Path('manifest.tsv').read_text().splitlines():
    name, w, h, n, *timings = row.split('\t')
    w, h, n = map(int, (w, h, n))
    timings = list(map(int, timings))
    raw = Path(name + '.rgba').read_bytes()
    size = w * h * 4
    alpha = [bytes(v >= 128 for v in raw[j*size+3:(j+1)*size:4]) for j in range(n)]
    fixed_alpha = all(a == alpha[0] for a in alpha)
    expected = []
    for j, delay in enumerate(timings):
        rgba = bytearray(raw[j*size:(j+1)*size])
        for k in range(0, size, 4):
            if rgba[k+3] < 128:
                rgba[k:k+4] = b'\0\0\0\0'
        if expected and expected[-1][0] == rgba:
            expected[-1][1] += delay
        else:
            expected.append([rgba, delay])
    actual_expected = []
    for rgba, delay in expected:
        while delay > 65535:
            actual_expected.append((rgba, 65535))
            delay -= 65535
        actual_expected.append((rgba, delay))
    with Image.open(name + '.gif') as im:
        assert im.size == (w, h), (name, im.size)
        assert im.info['loop'] == 0, name
        assert im.n_frames == len(actual_expected), (name, im.n_frames, len(actual_expected))
        for j, (rgba, delay) in enumerate(actual_expected):
            im.seek(j)
            assert im.disposal_method == (1 if fixed_alpha else 2), (name, j, im.disposal_method)
            actual = bytearray(im.convert('RGBA').tobytes())
            for k in range(0, len(actual), 4):
                if actual[k+3] == 0:
                    actual[k:k+4] = b'\0\0\0\0'
            if name == 'coarse_fallback':
                rgb_error = sum(abs(x-y) for k,(x,y) in enumerate(zip(actual,rgba)) if k%4!=3) / (w*h*3)
                assert rgb_error < 20, rgb_error
                assert actual[3::4] == rgba[3::4]
            else:
                assert actual == rgba, (name, j, 'RGBA pixels differ')
            assert im.info.get('duration', 0) == delay*10, (name, j, im.info)
    count += 1
print(f'PASS: {count} GIFs independently decoded by Pillow; exact RGBA or bounded quantization error, frame count, delay, transparency, loop, and correct disposal checked.')
