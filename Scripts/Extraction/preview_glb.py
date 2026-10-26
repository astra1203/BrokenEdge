"""Render a contact sheet of GLB meshes so bridge pieces can be identified visually.

Tiny software rasteriser: reads the glTF JSON + BIN chunk from a .glb, applies node
TRS, then painter-sorts triangles with flat shading and draws them with Pillow.
No external 3D renderer required.

    python Scripts/Extraction/preview_glb.py <glb-or-dir> <out.png>
"""
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

COMPONENT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2),
             5125: ('I', 4), 5126: ('f', 4)}
NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def load_glb(path):
    data = Path(path).read_bytes()
    assert data[:4] == b'glTF', path
    off, doc, blob = 12, None, b''
    while off < len(data):
        clen, ctype = struct.unpack_from('<II', data, off)
        chunk = data[off + 8:off + 8 + clen]
        if ctype == 0x4E4F534A:
            doc = json.loads(chunk.decode('utf-8'))
        elif ctype == 0x004E4942:
            blob = chunk
        off += 8 + clen
    return doc, blob


def read_accessor(doc, blob, index):
    acc = doc['accessors'][index]
    view = doc['bufferViews'][acc['bufferView']]
    fmt, size = COMPONENT[acc['componentType']]
    n = NCOMP[acc['type']]
    stride = view.get('byteStride') or size * n
    base = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    out = np.empty((acc['count'], n), dtype=np.dtype('<' + fmt))
    for i in range(acc['count']):
        out[i] = struct.unpack_from('<' + fmt * n, blob, base + i * stride)
    return out.astype(np.float64)


def node_matrix(node):
    if 'matrix' in node:
        return np.array(node['matrix'], dtype=np.float64).reshape(4, 4).T
    t = np.eye(4)
    if 'translation' in node:
        t[:3, 3] = node['translation']
    if 'rotation' in node:
        x, y, z, w = node['rotation']
        r = np.array([
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
        m = np.eye(4)
        m[:3, :3] = r
        t = t @ m
    if 'scale' in node:
        s = np.eye(4)
        s[0, 0], s[1, 1], s[2, 2] = node['scale']
        t = t @ s
    return t


def collect(doc, blob):
    """Return (positions[N,3], triangles[M,3]) in world space, applying node TRS."""
    store = {}

    def walk(idx, parent):
        node = doc['nodes'][idx]
        world = parent @ node_matrix(node)
        if 'mesh' in node:
            store.setdefault(node['mesh'], []).append(world)
        for child in node.get('children', []):
            walk(child, world)

    scene = doc.get('scenes', [{}])[doc.get('scene', 0)]
    for root in scene.get('nodes', []):
        walk(root, np.eye(4))
    verts, tris = [], []
    base = 0
    for mi, mesh in enumerate(doc.get('meshes', [])):
        for prim in mesh['primitives']:
            pos = read_accessor(doc, blob, prim['attributes']['POSITION'])
            if 'indices' in prim:
                idx = read_accessor(doc, blob, prim['indices']).astype(np.int64).ravel()
            else:
                idx = np.arange(len(pos))
            for world in store.get(mi, [np.eye(4)]):
                hom = np.hstack([pos, np.ones((len(pos), 1))]) @ world.T
                verts.append(hom[:, :3])
                tris.append(idx.reshape(-1, 3) + base)
                base += len(pos)
    return np.vstack(verts), np.vstack(tris)


def render(verts, tris, size=(520, 400), yaw=35.0, pitch=22.0, bg=(22, 26, 32)):
    w, h = size
    img = Image.new('RGB', size, bg)
    draw = ImageDraw.Draw(img)
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    rz = np.array([[cy, -sy, 0], [sy, cy, 0], [0, 0, 1]])
    rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    rot = rx @ rz
    # glTF is Y-up: treat Y as vertical for display
    p = verts - (verts.min(0) + verts.max(0)) / 2
    p = p @ rot.T
    span = max(np.ptp(p[:, 0]), np.ptp(p[:, 1])) or 1.0
    scale = 0.86 * min(w, h) / span
    sx = (p[:, 0] * scale + w / 2)
    sy2 = (-p[:, 1] * scale + h / 2)
    depth = p[:, 2]

    tri = tris
    v0, v1, v2 = p[tri[:, 0]], p[tri[:, 1]], p[tri[:, 2]]
    n = np.cross(v1 - v0, v2 - v0)
    length = np.linalg.norm(n, axis=1)
    length[length == 0] = 1
    n = n / length[:, None]
    light = np.array([0.35, 0.55, 0.75])
    light = light / np.linalg.norm(light)
    shade = np.abs(n @ light)
    order = np.argsort((depth[tri[:, 0]] + depth[tri[:, 1]] + depth[tri[:, 2]]) / 3)
    for t in order:
        a, b, c = tri[t]
        base_c = 132
        level = int(np.clip(base_c * (0.35 + 0.75 * shade[t]), 12, 245))
        col = (level, int(level * 0.98), int(level * 0.92))
        draw.polygon([(sx[a], sy2[a]), (sx[b], sy2[b]), (sx[c], sy2[c])], fill=col)
    return img, scale


def main():
    src = Path(sys.argv[1])
    out = Path(sys.argv[2])
    files = sorted(src.rglob('*.glb')) if src.is_dir() else [src]
    tiles = []
    for f in files:
        doc, blob = load_glb(f)
        verts, tris = collect(doc, blob)
        img, scale = render(verts, tris)
        label = f.stem.replace('SM_H_Building_FutureTech_Bridge_', '')
        dims = [round(x / 100, 1) for x in np.ptp(verts, axis=0)]
        ImageDraw.Draw(img).text((10, 8), f'{label}  {dims[0]}x{dims[1]}x{dims[2]} m  {len(tris)} tri',
                                 fill=(236, 240, 246))
        tiles.append(img)
        print(f'{label:5s} dims(m)={dims} tris={len(tris)}')
    cols = min(4, len(tiles))
    rows = (len(tiles) + cols - 1) // cols
    w, h = tiles[0].size
    sheet = Image.new('RGB', (w * cols, h * rows), (16, 19, 24))
    for i, t in enumerate(tiles):
        sheet.paste(t, ((i % cols) * w, (i // cols) * h))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print('SHEET', out, sheet.size)


if __name__ == '__main__':
    main()
