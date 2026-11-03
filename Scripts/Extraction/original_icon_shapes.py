"""Project-original item icon designs and a tiny pure-Python rasterizer (no third-party imaging library).

Every design is a set of flat silhouettes in a box of height 1 and width ASPECT; generate_original_icons.py
renders each design once to a PNG and assigns it to the items that use it. Nothing here is traced from or
derived from any third-party artwork.
"""
import math
import struct
import zlib

BASE = (0xC9, 0xCF, 0xD6)
DARK = (0x4F, 0x58, 0x62)


def rect(x, y, w, h):
    return [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]


def ellipse(cx, cy, rx, ry, start=0.0, end=360.0, steps=48):
    points = []
    for i in range(steps + 1):
        a = math.radians(start + (end - start) * i / steps)
        points.append((cx + rx * math.cos(a), cy + ry * math.sin(a)))
    return points


def circle(cx, cy, r):
    return ellipse(cx, cy, r, r)


def rounded(x, y, w, h, r):
    points = []
    for cx, cy, a0 in ((x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)):
        points += ellipse(cx, cy, r, r, a0, a0 + 90, 8)
    return points


# 设计名 -> (宽高比, [(颜色, [多边形...]), ...])，按顺序叠加。
DESIGNS = {
    'rifle': (3.2, [
        (BASE, [[(0.05, 0.30), (0.75, 0.36), (0.75, 0.58), (0.18, 0.66), (0.05, 0.62)],
                rect(0.72, 0.30, 1.0, 0.26), rect(1.70, 0.33, 0.80, 0.20), rect(2.48, 0.39, 0.62, 0.07),
                rect(3.02, 0.36, 0.12, 0.12), rect(0.85, 0.24, 0.95, 0.07),
                [(1.30, 0.56), (1.55, 0.56), (1.64, 0.92), (1.40, 0.95)],
                [(0.88, 0.56), (1.05, 0.56), (1.00, 0.86), (0.84, 0.84)]]),
        (DARK, [rect(1.12, 0.36, 0.30, 0.06)] + [rect(1.80 + 0.15 * i, 0.40, 0.08, 0.06) for i in range(5)]),
    ]),
    'ammo': (1.2, [
        (BASE, sum(([rect(x, 0.38, 0.24, 0.5), [(x, 0.38), (x + 0.12, 0.08), (x + 0.24, 0.38)]]
                    for x in (0.15, 0.48, 0.81)), [])),
        (DARK, sum(([rect(x, 0.80, 0.24, 0.04), rect(x, 0.38, 0.24, 0.04)] for x in (0.15, 0.48, 0.81)), [])),
    ]),
    'medkit': (1.25, [
        (BASE, [rounded(0.08, 0.22, 1.09, 0.70, 0.08), rounded(0.42, 0.08, 0.41, 0.18, 0.05)]),
        (DARK, [rect(0.50, 0.14, 0.25, 0.08), rect(0.555, 0.34, 0.14, 0.46), rect(0.395, 0.50, 0.46, 0.14)]),
    ]),
    'bandage': (1.0, [
        (BASE, [circle(0.46, 0.46, 0.38), rect(0.46, 0.70, 0.46, 0.14)]),
        (DARK, [circle(0.46, 0.46, 0.14), rect(0.46, 0.70, 0.04, 0.14)]),
    ]),
    'splint': (2.0, [
        (BASE, [rect(0.1, 0.30, 1.8, 0.14), rect(0.1, 0.56, 1.8, 0.14)]),
        (DARK, [rect(x, 0.24, 0.10, 0.52) for x in (0.45, 0.95, 1.45)]),
    ]),
    'syringe': (2.4, [
        (BASE, [rect(0.5, 0.38, 1.3, 0.24), rect(0.15, 0.44, 0.35, 0.12), rect(0.42, 0.28, 0.08, 0.44),
                rect(0.08, 0.34, 0.08, 0.32), rect(1.8, 0.48, 0.5, 0.04)]),
        (DARK, [rect(0.7 + 0.2 * i, 0.38, 0.03, 0.10) for i in range(5)]),
    ]),
    'pills': (0.8, [
        (BASE, [rounded(0.15, 0.30, 0.50, 0.62, 0.06), rect(0.12, 0.10, 0.56, 0.20)]),
        (DARK, [rect(0.20, 0.48, 0.40, 0.26), rect(0.12, 0.26, 0.56, 0.03)]),
    ]),
    'document': (0.8, [
        (BASE, [[(0.10, 0.05), (0.55, 0.05), (0.70, 0.20), (0.70, 0.95), (0.10, 0.95)]]),
        (DARK, [rect(0.18, 0.30 + 0.1 * i, 0.42 - (0.12 if i == 5 else 0), 0.04) for i in range(6)]
         + [[(0.55, 0.05), (0.55, 0.20), (0.70, 0.20)]]),
    ]),
    'chip': (1.0, [
        (BASE, [rect(0.22, 0.22, 0.56, 0.56)]
         + sum(([rect(0.27 + 0.11 * i, 0.10, 0.05, 0.12), rect(0.27 + 0.11 * i, 0.78, 0.05, 0.12),
                 rect(0.10, 0.27 + 0.11 * i, 0.12, 0.05), rect(0.78, 0.27 + 0.11 * i, 0.12, 0.05)]
                for i in range(5)), [])),
        (DARK, [rect(0.33, 0.33, 0.34, 0.34)]),
        (BASE, [circle(0.40, 0.40, 0.03)]),
    ]),
    'bar': (1.6, [
        (BASE, [[(0.20, 0.72), (1.40, 0.72), (1.25, 0.34), (0.35, 0.34)]]),
        (DARK, [[(0.42, 0.42), (1.18, 0.42), (1.20, 0.46), (0.40, 0.46)], rect(0.20, 0.68, 1.20, 0.04)]),
    ]),
    'gem': (1.0, [
        (BASE, [[(0.25, 0.22), (0.75, 0.22), (0.92, 0.42), (0.50, 0.90), (0.08, 0.42)]]),
        (DARK, [[(0.08, 0.42), (0.92, 0.42), (0.89, 0.46), (0.11, 0.46)],
                [(0.36, 0.44), (0.40, 0.44), (0.52, 0.86), (0.49, 0.88)],
                [(0.60, 0.44), (0.64, 0.44), (0.52, 0.86), (0.50, 0.85)]]),
    ]),
    'bottle': (0.6, [
        (BASE, [rect(0.22, 0.05, 0.16, 0.20), [(0.22, 0.25), (0.38, 0.25), (0.50, 0.38), (0.10, 0.38)],
                rect(0.10, 0.38, 0.40, 0.55)]),
        (DARK, [rect(0.14, 0.55, 0.32, 0.20), rect(0.20, 0.10, 0.20, 0.03)]),
    ]),
    'device': (1.4, [
        (BASE, [rounded(0.10, 0.20, 1.20, 0.65, 0.06), rect(0.30, 0.85, 0.12, 0.06), rect(0.98, 0.85, 0.12, 0.06)]),
        (DARK, [rect(0.20, 0.30, 0.62, 0.44), circle(1.03, 0.40, 0.08), circle(1.03, 0.64, 0.08)]),
    ]),
    'crate': (1.2, [
        (BASE, [rect(0.10, 0.28, 1.00, 0.62), rect(0.06, 0.18, 1.08, 0.12)]),
        (DARK, [rect(0.10, 0.58, 1.00, 0.03), rect(0.06, 0.30, 1.08, 0.03), rect(0.52, 0.34, 0.16, 0.12)]),
    ]),
    'can': (0.8, [
        (BASE, [rounded(0.15, 0.16, 0.50, 0.78, 0.06)]),
        (DARK, [rect(0.15, 0.25, 0.50, 0.03), rect(0.15, 0.84, 0.50, 0.03), rect(0.20, 0.42, 0.40, 0.28)]),
    ]),
    'card': (0.75, [
        (BASE, [rounded(0.12, 0.08, 0.52, 0.84, 0.05)]),
        (DARK, [[(0.38, 0.32), (0.50, 0.50), (0.38, 0.68), (0.26, 0.50)], rect(0.17, 0.13, 0.06, 0.10),
                rect(0.53, 0.77, 0.06, 0.10)]),
    ]),
    'coin': (1.0, [
        (BASE, [circle(0.50, 0.50, 0.40)]),
        (DARK, [circle(0.50, 0.50, 0.32)]),
        (BASE, [circle(0.50, 0.50, 0.28)]),
        (DARK, [rect(0.44, 0.34, 0.12, 0.32)]),
    ]),
    'statue': (0.8, [
        (BASE, [rect(0.15, 0.80, 0.50, 0.12), [(0.27, 0.80), (0.53, 0.80), (0.50, 0.44), (0.30, 0.44)],
                circle(0.40, 0.30, 0.13)]),
        (DARK, [rect(0.15, 0.80, 0.50, 0.03), rect(0.33, 0.56, 0.14, 0.03)]),
    ]),
    'cable': (1.1, [
        (BASE, [circle(0.45, 0.48, 0.38), rect(0.78, 0.70, 0.18, 0.14)]),
        (DARK, [circle(0.45, 0.48, 0.27)]),
        (BASE, [circle(0.45, 0.48, 0.20)]),
        (DARK, [circle(0.45, 0.48, 0.09), rect(0.96, 0.72, 0.08, 0.03), rect(0.96, 0.79, 0.08, 0.03)]),
    ]),
    'tool': (2.0, [
        (BASE, [rect(0.32, 0.42, 1.30, 0.16), circle(0.26, 0.50, 0.22), circle(1.66, 0.50, 0.15)]),
        (DARK, [rect(0.0, 0.43, 0.24, 0.14), circle(1.66, 0.50, 0.06)]),
    ]),
    'helmet': (1.2, [
        (BASE, [ellipse(0.60, 0.66, 0.48, 0.52, 180, 360), rect(0.06, 0.62, 1.08, 0.10)]),
        (DARK, [rect(0.22, 0.72, 0.07, 0.18), rect(0.91, 0.72, 0.07, 0.18), rect(0.30, 0.44, 0.60, 0.03)]),
    ]),
    'vest': (0.9, [
        (BASE, [[(0.12, 0.20), (0.30, 0.08), (0.38, 0.22), (0.52, 0.22), (0.60, 0.08), (0.78, 0.20),
                 (0.80, 0.92), (0.10, 0.92)]]),
        (DARK, [rect(0.18, 0.56, 0.16, 0.22), rect(0.37, 0.56, 0.16, 0.22), rect(0.56, 0.56, 0.16, 0.22),
                rect(0.10, 0.44, 0.70, 0.03)]),
    ]),
    'rig': (1.2, [
        (BASE, [rect(0.10, 0.44, 1.00, 0.42), rect(0.24, 0.08, 0.11, 0.38), rect(0.85, 0.08, 0.11, 0.38)]),
        (DARK, [rect(0.18 + 0.22 * i, 0.50, 0.16, 0.30) for i in range(4)]),
    ]),
    'backpack': (0.9, [
        (BASE, [rounded(0.15, 0.20, 0.60, 0.72, 0.12), rounded(0.32, 0.05, 0.26, 0.18, 0.06)]),
        (DARK, [rect(0.38, 0.10, 0.14, 0.08), rect(0.25, 0.56, 0.40, 0.26), rect(0.15, 0.34, 0.60, 0.04)]),
    ]),
    'safe': (1.0, [
        (BASE, [rounded(0.12, 0.12, 0.76, 0.76, 0.05), rect(0.20, 0.88, 0.10, 0.06), rect(0.70, 0.88, 0.10, 0.06)]),
        (DARK, [rect(0.20, 0.20, 0.60, 0.60)]),
        (BASE, [circle(0.46, 0.50, 0.15), rect(0.68, 0.36, 0.05, 0.28)]),
        (DARK, [circle(0.46, 0.50, 0.05)]),
    ]),
    'scope': (2.4, [
        (BASE, [rect(0.40, 0.38, 1.60, 0.24), rect(1.80, 0.30, 0.50, 0.40), rect(0.10, 0.32, 0.40, 0.36),
                rect(1.05, 0.20, 0.20, 0.18), rect(0.70, 0.62, 0.12, 0.14), rect(1.50, 0.62, 0.12, 0.14),
                rect(0.60, 0.74, 1.10, 0.08)]),
        (DARK, [rect(2.26, 0.36, 0.04, 0.28), rect(0.10, 0.36, 0.04, 0.28)]),
    ]),
    'grip': (0.7, [
        (BASE, [[(0.15, 0.14), (0.55, 0.14), (0.50, 0.92), (0.20, 0.92)], rect(0.05, 0.05, 0.60, 0.10)]),
        (DARK, [rect(0.22, 0.30 + 0.14 * i, 0.26, 0.04) for i in range(4)]),
    ]),
    'muzzle': (2.0, [
        (BASE, [rect(0.10, 0.35, 1.70, 0.30), rect(1.80, 0.42, 0.12, 0.16)]),
        (DARK, [rect(0.30 + 0.25 * i, 0.42, 0.12, 0.16) for i in range(3)]),
    ]),
    'magazine': (0.8, [
        (BASE, [[(0.22, 0.05), (0.58, 0.05), (0.70, 0.95), (0.36, 0.95)]]),
        (DARK, [[(0.30, 0.20), (0.50, 0.20), (0.52, 0.24), (0.31, 0.24)]]),
    ]),
    'rail': (1.6, [
        (BASE, [rect(0.10, 0.42, 1.40, 0.20)] + [rect(0.15 + 0.18 * i, 0.32, 0.10, 0.12) for i in range(8)]),
        (DARK, [rect(0.10, 0.56, 1.40, 0.03)]),
    ]),
    'key': (2.0, [
        (BASE, [circle(0.35, 0.50, 0.28), rect(0.60, 0.44, 1.20, 0.12), rect(1.40, 0.56, 0.10, 0.14),
                rect(1.60, 0.56, 0.10, 0.20), rect(1.20, 0.56, 0.08, 0.10)]),
        (DARK, [circle(0.35, 0.50, 0.10)]),
    ]),
    'keycard': (1.6, [
        (BASE, [rounded(0.08, 0.10, 1.44, 0.80, 0.08)]),
        (DARK, [rect(0.20, 0.34, 0.30, 0.24), rect(0.08, 0.70, 1.44, 0.08), rect(0.65, 0.30, 0.70, 0.06),
                rect(0.65, 0.45, 0.50, 0.06)]),
    ]),
    'grenade': (0.8, [
        (BASE, [ellipse(0.40, 0.60, 0.30, 0.34), rect(0.30, 0.16, 0.20, 0.14),
                [(0.48, 0.18), (0.62, 0.20), (0.66, 0.55), (0.60, 0.56), (0.54, 0.24)], circle(0.20, 0.20, 0.08)]),
        (DARK, [circle(0.20, 0.20, 0.04), rect(0.12, 0.58, 0.56, 0.03)]),
    ]),
}


def _fill(mask, width, height, polygon):
    """Even-odd scanline fill of one polygon into a 0/1 bytearray mask."""
    edges = list(zip(polygon, polygon[1:] + polygon[:1]))
    top = max(0, int(min(p[1] for p in polygon)))
    bottom = min(height - 1, int(max(p[1] for p in polygon)) + 1)
    for row in range(top, bottom + 1):
        y = row + 0.5
        xs = sorted(x0 + (y - y0) * (x1 - x0) / (y1 - y0)
                    for (x0, y0), (x1, y1) in edges if (y0 <= y < y1) or (y1 <= y < y0))
        base = row * width
        for a, b in zip(xs[0::2], xs[1::2]):
            start = max(0, math.ceil(a - 0.5))
            end = min(width - 1, math.floor(b - 0.5))
            if end >= start:
                mask[base + start:base + end + 1] = b'\x01' * (end - start + 1)


def render(name, height=200, supersample=4, margin=0.06):
    """Return (width, height, RGBA bytes) for one design."""
    aspect, layers = DESIGNS[name]
    width = max(1, round(height * aspect))
    hw, hh = width * supersample, height * supersample
    inner = 1 - 2 * margin
    to_px = lambda p: ((margin * aspect + p[0] * inner) * height * supersample, (margin + p[1] * inner) * height * supersample)
    pixels = [[0.0, 0.0, 0.0, 0.0] for _ in range(width * height)]
    area = supersample * supersample
    for color, polygons in layers:
        mask = bytearray(hw * hh)
        for polygon in polygons:
            _fill(mask, hw, hh, [to_px(p) for p in polygon])
        for out_row in range(height):
            rows = [mask[(out_row * supersample + k) * hw:(out_row * supersample + k + 1) * hw] for k in range(supersample)]
            columns = [sum(c) for c in zip(*rows)]
            for x in range(width):
                coverage = sum(columns[x * supersample:(x + 1) * supersample]) / area
                if coverage <= 0:
                    continue
                pixel = pixels[out_row * width + x]
                alpha = coverage + pixel[3] * (1 - coverage)
                for c in range(3):
                    pixel[c] = (color[c] * coverage + pixel[c] * pixel[3] * (1 - coverage)) / alpha
                pixel[3] = alpha
    data = bytearray()
    for y in range(height):
        data.append(0)
        for p in pixels[y * width:(y + 1) * width]:
            data += bytes((round(p[0]), round(p[1]), round(p[2]), round(p[3] * 255)))
    return width, height, bytes(data)


def write_png(path, width, height, scanlines):
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(scanlines, 9)))
        f.write(chunk(b'IEND', b''))
