"""Convert a .glb to a centimetre-scaled .obj so the UE MCP importer can read it.

The MCP StaticMeshTools importer only accepts fbx/obj (FbxFactory). Unreal's OBJ
importer treats one unit as one centimetre, while CUE4Parse exports glTF in metres,
so vertices are multiplied by 100 here.

    python Scripts/Extraction/glb_to_obj.py <in.glb> <out.obj>
"""
import json
import struct
import sys
from pathlib import Path

COMPONENT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2),
             5125: ('I', 4), 5126: ('f', 4)}
NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}


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


def read(doc, blob, index):
    acc = doc['accessors'][index]
    view = doc['bufferViews'][acc['bufferView']]
    fmt, size = COMPONENT[acc['componentType']]
    n = NCOMP[acc['type']]
    stride = view.get('byteStride') or size * n
    base = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    rows = [struct.unpack_from('<' + fmt * n, blob, base + i * stride) for i in range(acc['count'])]
    return rows


def main():
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    doc, blob = load_glb(src)
    out = ['# converted from ' + src.name]
    offset = 1
    for mesh in doc.get('meshes', []):
        for prim in mesh['primitives']:
            pos = read(doc, blob, prim['attributes']['POSITION'])
            nrm = read(doc, blob, prim['attributes']['NORMAL']) if 'NORMAL' in prim['attributes'] else None
            for x, y, z in pos:
                out.append(f'v {x * 100:.4f} {z * 100:.4f} {y * 100:.4f}')
            if nrm:
                for x, y, z in nrm:
                    out.append(f'vn {x:.4f} {z:.4f} {y:.4f}')
            if 'indices' in prim:
                idx = [v[0] for v in read(doc, blob, prim['indices'])]
            else:
                idx = list(range(len(pos)))
            for i in range(0, len(idx) - 2, 3):
                a, b, c = idx[i] + offset, idx[i + 1] + offset, idx[i + 2] + offset
                if nrm:
                    out.append(f'f {a}//{a} {b}//{b} {c}//{c}')
                else:
                    out.append(f'f {a} {b} {c}')
            offset += len(pos)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text('\n'.join(out) + '\n', encoding='utf-8')
    print('OBJ', dst, len(out), 'lines')


if __name__ == '__main__':
    main()
