"""Measure the rendered reticle; this does not substitute for lens/rail visual review."""
import argparse
import json
import math
from pathlib import Path
import struct
import zlib


def rgb_rows(path):
    data = Path(path).read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', path
    offset, compressed = 8, bytearray()
    while offset < len(data):
        size = struct.unpack_from('>I', data, offset)[0]
        kind, chunk = data[offset+4:offset+8], data[offset+8:offset+8+size]
        if kind == b'IHDR':
            width, height, bits, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', chunk)
            assert bits == 8 and color in (2, 6) and not interlace and not compression and not filtering
        elif kind == b'IDAT':
            compressed.extend(chunk)
        offset += size + 12
    bpp = 3 if color == 2 else 4
    stride = width * bpp
    raw = zlib.decompress(compressed)
    assert len(raw) == height * (stride + 1)
    previous = bytearray(stride)
    rows = []
    for y in range(height):
        mode = raw[y*(stride+1)]
        row = bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        assert 0 <= mode <= 4
        for x in range(stride):
            a, b, c = (row[x-bpp] if x >= bpp else 0), previous[x], (previous[x-bpp] if x >= bpp else 0)
            if mode == 1: predictor = a
            elif mode == 2: predictor = b
            elif mode == 3: predictor = (a+b)//2
            elif mode == 4:
                p = a+b-c
                distances = [abs(p-a), abs(p-b), abs(p-c)]
                predictor = [a, b, c][distances.index(min(distances))]
            else: predictor = 0
            row[x] = (row[x] + predictor) & 255
        rows.append(row)
        previous = row
    return width, height, bpp, rows


def measure(path):
    width, height, bpp, rows = rgb_rows(path)
    pixels = []
    # Limit the search to the optic, excluding HUD labels and unrelated scenery.
    for y in range(height//2-40, height//2+41):
        for x in range(width//2-40, width//2+41):
            r, g, b = rows[y][x*bpp:x*bpp+3]
            if r >= 130 and r > 1.7*g and r > 1.7*b:
                pixels.append((x+.5, y+.5, r-max(g, b)))
    if not pixels:
        return {'file': str(path), 'size': [width, height], 'passed': False, 'error': 'No visible reticle'}
    weight = sum(p[2] for p in pixels)
    center = [sum(p[i]*p[2] for p in pixels)/weight for i in (0, 1)]
    error = math.dist(center, [width/2, height/2])
    return {'file': str(path), 'size': [width, height], 'red_pixels': len(pixels),
            'center': center, 'center_error_px': error, 'passed': error <= 2}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('images', nargs='+', type=Path)
    args = parser.parse_args()
    results = [measure(p) for p in args.images]
    print(json.dumps(results, indent=2))
    raise SystemExit(0 if all(r['passed'] for r in results) else 1)
