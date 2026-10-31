from PIL import Image, ImageDraw
from pathlib import Path
import numpy as np

raw = np.frombuffer(Path('integration.rgba').read_bytes(), dtype=np.uint8).reshape((48,206,480,4))
sheet = Image.new('RGB', (960, 452), '#dedede')
for pos, colors in enumerate((256,128,64,32)):
    expected_delay = [2000 if i % 16 == 0 else 40 for i in range(48)]
    with Image.open(f'integration_{colors}.gif') as im:
        assert im.n_frames == 48, im.n_frames
        assert im.info['loop'] == 0
        error_sum = 0
        pixel_count = 0
        maximum_error = 0
        total_ms = 0
        for i in range(im.n_frames):
            im.seek(i)
            assert im.info['duration'] == expected_delay[i]
            assert im.disposal_method == 1
            decoded = np.array(im.convert('RGBA'))
            mask = raw[i,:,:,3] >= 128
            assert np.array_equal(decoded[:,:,3] == 255, mask)
            error = abs(decoded[:,:,:3].astype(np.int16) - raw[i,:,:,:3].astype(np.int16))[mask]
            error_sum += error.sum()
            pixel_count += error.size
            maximum_error = max(maximum_error, error.max())
            total_ms += im.info['duration']
        assert total_ms == 7800
        im.seek(0)
        thumb = im.convert('RGBA')
        x, y = (pos%2)*480, (pos//2)*226
        sheet.paste(thumb, (x, y+20), thumb)
        ImageDraw.Draw(sheet).text((x+10,y+3), f'{colors} colors', fill='black')
        print(f'{colors} colors: MAE={error_sum/pixel_count:.3f}/255 max_channel_error={maximum_error}, {total_ms}ms exact duration, alpha mask exact')
sheet.save('integration_contact_sheet.png')
