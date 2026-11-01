import collections
import hashlib
import json
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

path = Path(sys.argv[1])
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
size = path.stat().st_size
counts = collections.Counter()
frames, errors, versions = [], [], []
header, raw, white_balance = {}, {}, {}
exposures, capture_info = [], {}
camera = None

with path.open('rb') as f:
    offset = 0
    while offset < size:
        f.seek(offset)
        h = f.read(16)
        if len(h) < 16:
            errors.append(f'Incomplete block header at {offset}')
            break
        tag, block_size, timestamp = struct.unpack('<4sIQ', h)
        tag = tag.decode('ascii', errors='replace')
        if block_size < 16 or offset + block_size > size:
            errors.append(f'Invalid {tag} block size {block_size} at {offset}')
            break
        counts[tag] += 1
        if tag == 'MLVI':
            f.seek(offset)
            vals = struct.unpack('<4sI8sQHHIHHIIII', f.read(52))
            keys = ['magic', 'block_size', 'version', 'guid', 'file_number', 'file_count',
                    'flags', 'video_class', 'audio_class', 'video_frame_count',
                    'audio_frame_count', 'fps_numerator', 'fps_denominator']
            header = dict(zip(keys, vals))
            for key in ['magic', 'version']:
                header[key] = header[key].rstrip(b'\0').decode()
            del header['guid']
        elif tag == 'RAWI':
            width, height = struct.unpack('<HH', f.read(4))
            v = struct.unpack('<40i', f.read(160))
            raw = dict(width=width, height=height, sensor_buffer_height=v[2],
                       sensor_buffer_width=v[3], pitch=v[4], frame_size=v[5],
                       bits_per_pixel=v[6], black=v[7], white=v[8],
                       crop=v[9:13], active_area=v[13:17], cfa_pattern=hex(v[19]),
                       calibration_illuminant=v[20], color_matrix=v[21:39])
        elif tag == 'VIDF':
            number, cx, cy, px, py, space = struct.unpack('<IHHHHI', f.read(16))
            frames.append(dict(number=number, timestamp_us=timestamp, block_offset=offset,
                               block_size=block_size, data_offset=offset + 32 + space,
                               available_bytes=block_size - 32 - space,
                               crop_position=[cx, cy], pan_position=[px, py]))
        elif tag == 'EXPO':
            vals = struct.unpack('<4IQ', f.read(24))
            exposure = dict(zip(['iso_mode', 'iso', 'analog_iso', 'digital_gain', 'shutter_microseconds'], vals))
            exposure['timestamp_us'] = timestamp
            exposures.append(exposure)
        elif tag == 'RAWC':
            vals = struct.unpack('<4H4B2h', f.read(16))
            capture_info = dict(zip(['sensor_x', 'sensor_y', 'sensor_crop', 'reserved',
                                     'binning_x', 'skipping_x', 'binning_y', 'skipping_y',
                                     'offset_x', 'offset_y'], vals))
        elif tag == 'IDNT':
            camera = f.read(32).split(b'\0')[0].decode(errors='replace')
        elif tag == 'WBAL':
            v = struct.unpack('<7I', f.read(28))
            white_balance = dict(zip(['mode', 'kelvin', 'red', 'green', 'blue', 'shift_gm', 'shift_ba'], v))
        elif tag == 'VERS':
            length, = struct.unpack('<I', f.read(4))
            versions.append(f.read(min(length, block_size - 20)).split(b'\0')[0].decode(errors='replace'))
        offset += block_size

def unpack14(data, width, height):
    words = np.frombuffer(data, dtype='<u2').reshape(-1, 7)
    p = np.empty((len(words), 8), dtype=np.uint16)
    p[:, 0] = words[:, 0] >> 2
    p[:, 1] = ((words[:, 0] & 3) << 12) | (words[:, 1] >> 4)
    p[:, 2] = ((words[:, 1] & 15) << 10) | (words[:, 2] >> 6)
    p[:, 3] = ((words[:, 2] & 63) << 8) | (words[:, 3] >> 8)
    p[:, 4] = ((words[:, 3] & 255) << 6) | (words[:, 4] >> 10)
    p[:, 5] = ((words[:, 4] & 1023) << 4) | (words[:, 5] >> 12)
    p[:, 6] = ((words[:, 5] & 4095) << 2) | (words[:, 6] >> 14)
    p[:, 7] = words[:, 6] & 16383
    return p.reshape(height, width)

def preview(p, index):
    a = np.maximum(p.astype(np.float32) - raw['black'], 0)
    # Quick half-resolution RGGB preview. Approximate daylight color rendering;
    # this is for validating scene content, not a final RAW development.
    rgb = np.stack([a[0::2, 0::2], (a[0::2, 1::2] + a[1::2, 0::2]) / 2,
                    a[1::2, 1::2]], axis=-1)
    rgb /= max(1, raw['white'] - raw['black'])
    matrix = np.array(raw['color_matrix'], dtype=np.float64).reshape(9, 2)
    camera_from_xyz = (matrix[:, 0] / matrix[:, 1]).reshape(3, 3)
    xyz_from_srgb = np.array([[.4124564, .3575761, .1804375],
                              [.2126729, .7151522, .0721750],
                              [.0193339, .1191920, .9503041]])
    srgb_from_camera = np.linalg.inv(camera_from_xyz @ xyz_from_srgb)
    rgb = np.clip(rgb @ srgb_from_camera.T, 0, 1)
    rgb = np.where(rgb <= .0031308, 12.92 * rgb, 1.055 * rgb ** (1 / 2.4) - .055)
    image_path = out / f'raw-frame-{index:04d}-preview.jpg'
    Image.fromarray(np.uint8(rgb * 255)).save(image_path, quality=95)
    return str(image_path)

previews = []
if header.get('video_class') != 1:
    errors.append('Video is not plain uncompressed RAW')
if raw.get('bits_per_pixel') != 14:
    errors.append('Unsupported RAW bit depth for this inspector')
if not errors:
    expected_bytes = raw['width'] * raw['height'] * 14 // 8
    sample_indices = {0, len(frames) // 2, len(frames) - 1}
    previous = None
    with path.open('rb') as f:
        for i, frame in enumerate(frames):
            if frame['available_bytes'] < expected_bytes:
                errors.append(f'Frame {i} RAW payload is short')
                continue
            f.seek(frame['data_offset'])
            data = f.read(expected_bytes)
            frame['sha256'] = hashlib.sha256(data).hexdigest()
            p = unpack14(data, raw['width'], raw['height'])
            frame['pixel_min'] = int(p.min())
            frame['pixel_max'] = int(p.max())
            frame['pixel_mean'] = float(p.mean())
            frame['pixel_sd'] = float(p.std())
            frame['zero_pixels'] = int(np.count_nonzero(p == 0))
            frame['black_or_below_fraction'] = float(np.mean(p <= raw['black']))
            frame['saturated_fraction'] = float(np.mean(p >= raw['white']))
            if previous is not None:
                frame['mean_absolute_change_from_previous'] = float(np.mean(np.abs(p.astype(np.int32) - previous)))
            previous = p.astype(np.int32)
            if i in sample_indices:
                previews.append(preview(p, i))

ordered = sorted(frames, key=lambda x: x['number'])
numbers = [x['number'] for x in ordered]
ts = np.array([x['timestamp_us'] for x in ordered], dtype=np.int64)
intervals = np.diff(ts)
fps = header.get('fps_numerator', 0) / max(1, header.get('fps_denominator', 1))
report = dict(file=str(path), file_bytes=size, camera=camera, header=header, raw=raw,
              white_balance=white_balance, exposure_metadata=exposures, capture_metadata=capture_info,
              versions=versions, block_counts=dict(counts),
              complete_file_structure=(offset == size and not errors), errors=errors,
              actual_video_frames=len(frames), header_frame_count_matches=len(frames) == header.get('video_frame_count'),
              fps=fps, duration_at_header_fps_seconds=len(frames) / fps if fps else None,
              sequential_frame_numbers=bool(numbers) and numbers == list(range(numbers[0], numbers[-1] + 1)),
              first_frame_number=numbers[0] if numbers else None,
              last_frame_number=numbers[-1] if numbers else None,
              frame_interval_us=dict(min=int(intervals.min()), median=float(np.median(intervals)), max=int(intervals.max())) if len(intervals) else None,
              timestamp_span_seconds=float(ts[-1] - ts[0]) / 1e6 if len(ts) else None,
              measured_fps_from_timestamps=(len(ts) - 1) * 1e6 / float(ts[-1] - ts[0]) if len(ts) > 1 else None,
              duration_from_timestamps_seconds=float(ts[-1] - ts[0] + np.median(intervals)) / 1e6 if len(intervals) else None,
              distinct_frame_payloads=len(set(x.get('sha256') for x in frames)),
              nonflat_frames=sum(x.get('pixel_sd', 0) > 0 for x in frames),
              zero_pixel_counts=dict(min=min(x.get('zero_pixels', -1) for x in frames), max=max(x.get('zero_pixels', -1) for x in frames)) if frames else None,
              previews=previews, frames=frames)
(out / 'inspection.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({k: v for k, v in report.items() if k != 'frames'}, indent=2))
