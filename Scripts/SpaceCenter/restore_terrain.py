"""Create seam-consistent native-import tiles from verified, unchanged height samples."""
import argparse
import gzip
import json
import math
import struct
from restoration_common import ROOT, WORK, ORIGIN, read, sha, write_json, input_path


def glb(path, positions, normals, uv, indices):
    binary, views, accessors = bytearray(), [], []
    def buffer(values, fmt, kind, component, target):
        offset = len(binary)
        for value in values:
            binary.extend(struct.pack(fmt, *value) if isinstance(value, (list, tuple)) else struct.pack(fmt, value))
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(binary) - offset, 'target': target})
        while len(binary) % 4:
            binary.append(0)
        accessor = {'bufferView': len(views) - 1, 'componentType': component, 'count': len(values), 'type': kind}
        if kind == 'VEC3':
            accessor.update(min=[min(v[i] for v in values) for i in range(3)], max=[max(v[i] for v in values) for i in range(3)])
        accessors.append(accessor)
        return len(accessors) - 1
    p = buffer(positions, '<3f', 'VEC3', 5126, 34962)
    n = buffer(normals, '<3f', 'VEC3', 5126, 34962)
    t = buffer(uv, '<2f', 'VEC2', 5126, 34962)
    ix = buffer(indices, '<I', 'SCALAR', 5125, 34963)
    doc = {'asset': {'version': '2.0', 'generator': 'BrokenEdge verified source-height reconstruction'},
           'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0, 'name': path.stem}],
           'meshes': [{'name': path.stem, 'primitives': [{'attributes': {'POSITION': p, 'NORMAL': n, 'TEXCOORD_0': t}, 'indices': ix}]}],
           'buffers': [{'byteLength': len(binary)}], 'bufferViews': views, 'accessors': accessors}
    encoded = json.dumps(doc, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    path.write_bytes(struct.pack('<III', 0x46546C67, 2, 28 + len(encoded) + len(binary)) +
                     struct.pack('<II', len(encoded), 0x4E4F534A) + encoded +
                     struct.pack('<II', len(binary), 0x004E4942) + binary)


def generate(tag):
    out = WORK / 'TerrainRuns' / tag
    out.mkdir(parents=True, exist_ok=False)
    original = input_path('terrain_source',ROOT / 'Artifacts/Extraction/Phase7BEvidence/terrain')
    report = read(original / 'Report.json')
    source = json.loads(gzip.decompress((original / 'TerrainSamples.json.gz').read_bytes()))
    assert sha(original / 'TerrainSamples.json.gz') == report['files']['TerrainSamples.json.gz']['sha256']
    scale, bias = report['height_scale'], report['height_bias']
    positions = [(x - ORIGIN[0], y - ORIGIN[1], ((value - 32768) / (scale * 128) + bias) * report['scale'][2] - ORIGIN[2])
                 for x, y, value, nx, ny in source['samples']]
    normals = [[0., 0., 0.] for _ in positions]
    for a, b, c in zip(*[iter(source['indices'])] * 3):
        ab = [positions[b][i] - positions[a][i] for i in range(3)]
        ac = [positions[c][i] - positions[a][i] for i in range(3)]
        normal = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
        assert normal[2] > 0, 'Source grid triangle faces downward'
        for vertex in [a, b, c]:
            for axis in range(3):
                normals[vertex][axis] += normal[axis]
    normals = [tuple(value / math.sqrt(sum(n*n for n in normal)) for value in normal) for normal in normals]
    entries, offset, errors, shared = [], 0, [], {}
    for patch in report['patches']:
        count = (patch['rect'][2] - 1) * (patch['rect'][3] - 1) * 6
        selected = source['indices'][offset:offset + count]
        offset += count
        ids = sorted(set(selected))
        mapping = {value: i for i, value in enumerate(ids)}
        anchor = [min(positions[v][axis] for v in ids) for axis in range(3)]
        local = [tuple((positions[v][axis] - anchor[axis]) / 100 for axis in [0, 2, 1]) for v in ids]
        shading = [tuple(normals[v][axis] for axis in [0, 2, 1]) for v in ids]
        uv = [(positions[v][0] / 100, positions[v][1] / 100) for v in ids]
        winding = [mapping[v] for a,b,c in zip(*[iter(selected)]*3) for v in (a,c,b)]
        name = 'SC_Terrain_%05d' % patch['node']
        target = out / (name + '.glb')
        glb(target, local, shading, uv, winding)
        for vertex, point in zip(ids, local):
            rounded = struct.unpack('<3f', struct.pack('<3f', *point))
            restored = [rounded[i]*100 + anchor[axis] for i,axis in enumerate([0,2,1])]
            errors.extend(abs(restored[i] - positions[vertex][axis]) for i,axis in enumerate([0,2,1]))
            encoded_normal = struct.pack('<3f', *shading[mapping[vertex]])
            if vertex in shared:
                assert encoded_normal == shared[vertex], 'Different boundary normal'
            shared[vertex] = encoded_normal
        entries.append({'id': name, 'source': 'terrain-node:' + str(patch['node']), 'node': patch['node'],
                        'file': str(target), 'sha256': sha(target), 'position': anchor,
                        'vertices': len(ids), 'triangles': count // 3, 'source_vertex_ids': ids})
    assert offset == len(source['indices'])
    assert max(errors) < .01
    write_json(out / 'Manifest.json', {'version': 1, 'origin': ORIGIN, 'tiles': entries,
                'source_sha256': sha(original / 'TerrainSamples.json.gz'), 'max_roundtrip_error_cm': max(errors),
                'normal_method': 'Area-weighted shared source-grid normals; original opaque normal bytes unchanged in source archive.',
                'triangulation': 'Explicit fixed diagonal, reversed for glTF coordinate reflection; original CDLOD topology not claimed.'})
    print('TERRAIN_TILES',len(entries),'ERROR_CM',max(errors),flush=True)


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    args=parser.parse_args()
    if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
        parser.error('Use a new alphanumeric tag')
    generate(args.tag)
