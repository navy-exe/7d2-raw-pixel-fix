"""Render a local, validated uncompressed 14-bit MLV capture as a viewing MP4."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np


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


def bilinear_channel(mosaic, row, column, green=False):
    sparse = np.zeros_like(mosaic)
    if green:
        sparse[0::2, 1::2] = mosaic[0::2, 1::2]
        sparse[1::2, 0::2] = mosaic[1::2, 0::2]
    else:
        sparse[row::2, column::2] = mosaic[row::2, column::2]
    padded = np.pad(sparse, 1, mode='reflect')
    axial = padded[:-2, 1:-1] + padded[2:, 1:-1] + padded[1:-1, :-2] + padded[1:-1, 2:]
    if green:
        return sparse + axial * np.float32(.25)
    diagonal = padded[:-2, :-2] + padded[:-2, 2:] + padded[2:, :-2] + padded[2:, 2:]
    return sparse + axial * np.float32(.5) + diagonal * np.float32(.25)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('inspection')
    parser.add_argument('output')
    parser.add_argument('--ffmpeg', required=True)
    parser.add_argument('--crop-left', type=int, default=0,
                        help='Remove this many columns from the viewing copy only')
    parser.add_argument('--crop-right', type=int, default=0,
                        help='Remove this many columns from the viewing copy only')
    args = parser.parse_args()
    report = json.loads(Path(args.inspection).read_text(encoding='utf-8'))
    if not report['complete_file_structure'] or report['errors']:
        raise ValueError('Input did not pass structural inspection')
    raw = report['raw']
    if report['header']['video_class'] != 1 or raw['bits_per_pixel'] != 14 or raw['cfa_pattern'] != '0x2010100':
        raise ValueError('Only uncompressed 14-bit RGGB is supported')
    source = Path(report['file'])
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise FileExistsError(output)
    frame_list = sorted(report['frames'], key=lambda item: item['number'])
    width, height = raw['width'], raw['height']
    if (args.crop_left < 0 or args.crop_right < 0 or
            args.crop_left % 2 or args.crop_right % 2):
        raise ValueError('Crop margins must be nonnegative even numbers to preserve Bayer alignment')
    output_width = width - args.crop_left - args.crop_right
    if output_width < 2 or output_width % 2 or height % 2:
        raise ValueError('The viewing copy must have positive even dimensions')
    crop_end = width - args.crop_right
    standards = [(24000, 1001), (24, 1), (25, 1), (30000, 1001), (30, 1), (50, 1), (60000, 1001), (60, 1)]
    actual_fps = report['measured_fps_from_timestamps']
    fps_n, fps_d = min(standards, key=lambda pair: abs(pair[0] / pair[1] - actual_fps))
    if abs(fps_n / fps_d - actual_fps) > .01:
        raise ValueError('Timestamp frame rate needs explicit interpretation')
    color = np.array(raw['color_matrix'], dtype=np.float64).reshape(9, 2)
    camera_from_xyz = (color[:, 0] / color[:, 1]).reshape(3, 3)
    xyz_from_srgb = np.array([[.4124564, .3575761, .1804375],
                              [.2126729, .7151522, .0721750],
                              [.0193339, .1191920, .9503041]])
    srgb_from_camera = np.linalg.inv(camera_from_xyz @ xyz_from_srgb).astype(np.float32)
    command = [args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-n',
               '-f', 'rawvideo', '-pixel_format', 'rgb24', '-video_size', f'{output_width}x{height}',
               '-framerate', f'{fps_n}/{fps_d}', '-i', 'pipe:0', '-an',
               '-c:v', 'libx264', '-preset', 'fast', '-crf', '18', '-pix_fmt', 'yuv420p',
               '-vf', 'scale=in_range=full:out_range=tv:out_color_matrix=bt709',
               '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'iec61966-2-1',
               '-color_range', 'tv', '-movflags', '+faststart',
               '-metadata', 'title=Canon 7D Mark II RAW recording - viewing preview',
               '-metadata', 'comment=Approximate daylight rendering of original RAW frames. No audio. Playback rate follows frame timestamps.',
               str(output)]
    log_path = output.with_suffix('.encode.log')
    expected_bytes = width * height * 14 // 8
    source_hash_before = hashlib.file_digest(source.open('rb'), 'sha256').hexdigest()
    with log_path.open('wb') as log:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=log)
        try:
            with source.open('rb') as handle:
                for index, frame in enumerate(frame_list):
                    handle.seek(frame['data_offset'])
                    data = handle.read(expected_bytes)
                    if hashlib.sha256(data).hexdigest() != frame['sha256']:
                        raise ValueError(f'Frame {frame["number"]} differs from inspected source')
                    mosaic = unpack14(data, width, height).astype(np.float32)
                    # Decode with the original row width before cropping. Even
                    # margins retain the RGGB phase and the original MLV stays intact.
                    mosaic = mosaic[:, args.crop_left:crop_end]
                    mosaic = np.maximum(mosaic - raw['black'], 0) / np.float32(raw['white'] - raw['black'])
                    sensor_rgb = np.stack([bilinear_channel(mosaic, 0, 0),
                                           bilinear_channel(mosaic, 0, 0, green=True),
                                           bilinear_channel(mosaic, 1, 1)], axis=-1)
                    rgb = np.clip(sensor_rgb @ srgb_from_camera.T, 0, 1)
                    rgb = np.where(rgb <= .0031308, 12.92 * rgb, 1.055 * rgb ** (1 / 2.4) - .055)
                    encoded_frame = np.ascontiguousarray(np.rint(rgb * 255), dtype=np.uint8)
                    process.stdin.write(encoded_frame.tobytes())
                    if (index + 1) % 100 == 0:
                        print(f'Rendered {index + 1}/{len(frame_list)} frames', flush=True)
            process.stdin.close()
            if process.wait() != 0:
                raise RuntimeError(log_path.read_text(errors='replace'))
        except BaseException:
            process.kill()
            process.wait()
            raise
    source_hash_after = hashlib.file_digest(source.open('rb'), 'sha256').hexdigest()
    if source_hash_after != source_hash_before:
        raise ValueError('Source file changed')
    # Decode the entire completed MP4 and verify its frame count.
    check = subprocess.run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-xerror',
                            '-i', str(output), '-map', '0:v:0', '-progress', 'pipe:1',
                            '-nostats', '-f', 'null', '-'], capture_output=True, text=True)
    if check.returncode:
        raise RuntimeError(check.stderr)
    decoded_count = int([line.split('=', 1)[1] for line in check.stdout.splitlines() if line.startswith('frame=')][-1])
    if decoded_count != len(frame_list):
        raise ValueError(f'Encoded frame count mismatch: {decoded_count}')
    summary = dict(output=str(output), source=str(source), source_sha256=source_hash_before,
                   source_unchanged=True, all_source_frame_hashes_verified=True,
                   resolution=[output_width, height], source_resolution=[width, height],
                   viewing_crop_columns=dict(left=args.crop_left, right=args.crop_right),
                   frames=decoded_count,
                   fps=f'{fps_n}/{fps_d}', duration_seconds=decoded_count * fps_d / fps_n,
                   audio=False, output_bytes=output.stat().st_size,
                   full_decode_verified=True, original_header_fps=report['fps'],
                   notes=['Approximate daylight color with bilinear demosaicing; viewing copy, not final grading.',
                          'Playback rate selected from original frame timestamps.',
                          ('Viewing copy has the requested margins removed; this does not calibrate the sensor active area.'
                           if args.crop_left or args.crop_right else
                           'Entire saved image is retained, including the dark left edge.')])
    output.with_suffix('.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':
    main()
