"""Decode bounded original CDLOD height samples; never fit or invent terrain.

The byte layout is independently checked against the complete source quadtree,
the CDLODComponent properties, all selected patch extrema and shared edges.
Output is source geometry for diagnostics, not an accepted material or UE map.
"""
import argparse
import collections
import gzip
import hashlib
import json
import math
from pathlib import Path
import struct

from phase7b_source import BASE_WORK, ROOT, TERRAIN, WORK, sha, write_json
from source_packages import extract, read_names

ASSETS = BASE_WORK / 'Source/DeltaForce/Content/Maps/BuildMaps_HD/SpaceCenter_Ma1/Runtime/LandscapeCDLOD/Asset'
ORIGIN = (664000., -451000., -22400.)
MINIMUM = (652000., -466000.)
MAXIMUM = (681000., -433000.)


def read_binary_asset(stem):
    asset, export = stem.with_suffix('.uasset').read_bytes(), stem.with_suffix('.uexp').read_bytes()
    if 'BinaryAsset' not in read_names(asset):
        raise ValueError('Not a source BinaryAsset: ' + str(stem))
    if len(export) < 24:
        raise ValueError('Truncated bulk header')
    flags, count, size, offset = struct.unpack_from('<IIIq', export)
    if flags != 0x40 or count != size or offset != len(asset) + 20 or len(export) != size + 24:
        raise ValueError('Unsupported or inconsistent inline bulk header: ' + str(stem))
    if struct.unpack_from('<I', export, len(export) - 4)[0] != 0x9E2A83C1:
        raise ValueError('Invalid package footer')
    return export[20:-4]


def parse_header(data):
    if len(data) < 100:
        raise ValueError('Truncated CDLOD header')
    info_at, info_size, nodes_at, nodes_size, tail_at, tail_size = struct.unpack_from('<6Q', data)
    if (info_at, info_size, nodes_at) != (48, 52, 100) or nodes_size % 96 or \
            tail_at != nodes_at + nodes_size or tail_at + tail_size != len(data):
        raise ValueError('Unsupported CDLOD section layout')
    info = struct.unpack_from('<7f6i', data, info_at)
    if info[7:10] != (32, 7, 512):
        raise ValueError('Unsupported CDLOD patch, LOD or tile dimensions')
    nodes = []
    for index, at in enumerate(range(nodes_at, tail_at, 96)):
        children = struct.unpack_from('<4i', data, at)
        parent = struct.unpack_from('<i', data, at + 16)[0]
        bounds = struct.unpack_from('<7f', data, at + 20)
        rect = struct.unpack_from('<4f', data, at + 48)
        offset, unused, tx, ty, lod = struct.unpack_from('<5i', data, at + 64)
        low, high = struct.unpack_from('<2f', data, at + 84)
        if not all(math.isfinite(n) for n in (*bounds, *rect, low, high)) or not 0 <= lod <= 7:
            raise ValueError('Invalid source node: ' + str(index))
        if tuple(bounds[3:5]) != (16. * 2 ** lod, 16. * 2 ** lod) or low > high:
            raise ValueError('Unexpected source patch bounds: ' + str(index))
        if any(int(n) != n for n in rect) or any(n < 0 for n in rect[:2]) or any(n < 1 for n in rect[2:]):
            raise ValueError('Unexpected source valid texel rectangle')
        if any(rect[i] + rect[i + 2] > 33 for i in (0, 1)):
            raise ValueError('Source texel rectangle escapes patch')
        nodes.append({'index': index, 'children': children, 'parent': parent, 'center': bounds[:3],
                      'extent': bounds[3:6], 'rect': tuple(map(int, rect)), 'offset': offset,
                      'tile': (tx, ty), 'lod': lod, 'minimum': low, 'maximum': high})
    if len(nodes) != sum(4 ** i for i in range(8)) or nodes[0]['parent'] != -1:
        raise ValueError('Incomplete source quadtree')
    for node in nodes:
        if node['lod'] == 0:
            if node['children'] != (-1,) * 4:
                raise ValueError('Leaf has child pointers')
        else:
            for child in node['children']:
                if not 0 <= child < len(nodes) or nodes[child]['parent'] != node['index'] or nodes[child]['lod'] != node['lod'] - 1:
                    raise ValueError('Invalid source quadtree link')
    return info, nodes, {'offset': tail_at, 'bytes': tail_size,
                         'sha256': hashlib.sha256(data[tail_at:]).hexdigest(),
                         'interpretation': 'Opaque source tail retained; not needed to locate selected LOD0 samples.'}


def write_glb(path, vertices, indices):
    # UE centimeters (X,Y,Z) -> glTF meters (X,Z,Y); the reflection reverses winding.
    positions = [(x / 100, z / 100, y / 100) for x, y, z in vertices]
    vertex_data = b''.join(struct.pack('<3f', *p) for p in positions)
    gltf_indices = [v for a, b, c in zip(indices[::3], indices[1::3], indices[2::3]) for v in (a, c, b)]
    index_data = struct.pack('<' + 'I' * len(gltf_indices), *gltf_indices)
    binary = vertex_data + index_data
    document = {'asset': {'version': '2.0', 'generator': 'BrokenEdge Phase7B original CDLOD diagnostic'},
                'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0}],
                'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'indices': 1}]}],
                'buffers': [{'byteLength': len(binary)}],
                'bufferViews': [{'buffer': 0, 'byteOffset': 0, 'byteLength': len(vertex_data), 'target': 34962},
                                {'buffer': 0, 'byteOffset': len(vertex_data), 'byteLength': len(index_data), 'target': 34963}],
                'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': len(positions), 'type': 'VEC3',
                               'min': [min(p[i] for p in positions) for i in range(3)],
                               'max': [max(p[i] for p in positions) for i in range(3)]},
                              {'bufferView': 1, 'componentType': 5125, 'count': len(indices), 'type': 'SCALAR'}]}
    encoded = json.dumps(document, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    path.write_bytes(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(encoded) + 8 + len(binary)) +
                     struct.pack('<II', len(encoded), 0x4E4F534A) + encoded +
                     struct.pack('<II', len(binary), 0x004E4942) + binary)
    max_error = 0.
    for original, converted in zip(vertices, struct.iter_unpack('<3f', vertex_data)):
        restored = (converted[0] * 100, converted[2] * 100, converted[1] * 100)
        max_error = max(max_error, max(abs(a - b) for a, b in zip(original, restored)))
    if max_error > 0.01:
        raise ValueError('glTF coordinate round trip exceeds 0.01 cm')
    return max_error


def recover(tag, source_probe):
    out = WORK / 'TerrainRuns' / tag
    out.mkdir(parents=True, exist_ok=False)
    parsed = json.loads((source_probe / 'Parsed/Packages.json').read_text(encoding='utf-8'))
    component = next(o['Properties'] for p in parsed for o in p.get('objects', []) if o.get('Type') == 'CDLODComponent')
    parent = next(o['Properties'] for p in parsed for o in p.get('objects', []) if o.get('Name') == 'DefaultSceneRoot')
    if parent.get('RelativeLocation') or parent.get('RelativeRotation') or parent.get('RelativeScale3D'):
        raise ValueError('Non-identity root requires explicit transform support')
    info, nodes, tail = parse_header(read_binary_asset(ASSETS / 'CDLODAsset_Header'))
    scale = tuple(component['RelativeScale3D'][k] for k in 'XYZ')
    if scale != info[4:7] or component['ValidPatchSize'] != info[7]:
        raise ValueError('CDLOD component disagrees with binary header')
    bias = component['HeightBiasInLocalSpace']
    # JSON prints the source float32 with a rounded decimal representation.
    height_scale = struct.unpack('<f', struct.pack('<f', component['HeightScaleInLocalSpace']))[0]
    if not math.isfinite(height_scale) or height_scale <= 0:
        raise ValueError('Invalid source height encoding scale')
    selected = [n for n in nodes if n['lod'] == 0 and all(
        (n['center'][i] + n['extent'][i]) * scale[i] >= MINIMUM[i] and
        (n['center'][i] - n['extent'][i]) * scale[i] <= MAXIMUM[i] for i in (0, 1))]
    if not selected:
        raise ValueError('No source terrain covers the bounded region')
    tile_ids = sorted({n['tile'] for n in selected})
    extraction = extract([TERRAIN + 'Asset/CDLODAsset_Heightmap_x%d_y%d' % tile for tile in tile_ids], limit_gib=1)
    write_json(out / 'SourceExtractions.json', extraction)
    if extraction['missing']:
        raise ValueError('Source height payload missing')
    tiles = {tile: read_binary_asset(ASSETS / ('CDLODAsset_Heightmap_x%d_y%d' % tile)) for tile in tile_ids}
    quantum = 1 / (height_scale * 128)
    samples, lookup, indices, patch_reports = [], {}, [], []
    shared, max_extrema_error = 0, 0.
    for node in selected:
        rx, ry, width, height = node['rect']
        count = (width + 2) * (height + 2) * 4
        block = tiles[node['tile']][node['offset']:node['offset'] + count]
        if len(block) != count:
            raise ValueError('Source height block escapes its payload')
        patch, heights = {}, []
        for y in range(height):
            for x in range(width):
                texel = ((y + 1) * (width + 2) + x + 1) * 4
                value = struct.unpack_from('>H', block, texel)[0]
                decoded = (value - 32768) * quantum + bias
                heights.append(decoded)
                point = tuple((node['center'][i] - node['extent'][i] + (rx + x if i == 0 else ry + y)) * scale[i] for i in (0, 1))
                raw_normal = list(block[texel + 2:texel + 4])
                if point in lookup:
                    index = lookup[point]
                    if samples[index][2] != value or samples[index][3:] != raw_normal:
                        raise ValueError('Original height or normal bytes disagree on shared edge: ' + str(point))
                    shared += 1
                else:
                    index = lookup[point] = len(samples)
                    samples.append([*point, value, *raw_normal])
                patch[x, y] = index
        extrema_errors = (min(heights) - node['minimum'], max(heights) - node['maximum'])
        error = max(map(abs, extrema_errors))
        # Source extrema prove floor quantization, not rounding to nearest:
        # every decoded extremum lies in [original - one quantum, original].
        if any(e < -quantum - 0.000001 or e > 0.000001 for e in extrema_errors):
            raise ValueError('Decoded extrema disagree with independent quadtree node %d: %.9f' % (node['index'], error))
        max_extrema_error = max(error, max_extrema_error)
        for y in range(height - 1):
            for x in range(width - 1):
                a, b, c, d = (patch[k] for k in [(x, y), (x + 1, y), (x + 1, y + 1), (x, y + 1)])
                indices.extend((a, b, c, a, c, d))
        patch_reports.append({'node': node['index'], 'tile': node['tile'], 'rect': node['rect'],
                              'byte_offset': node['offset'], 'byte_count': count,
                              'source_block_sha256': hashlib.sha256(block).hexdigest(),
                              'source_minimum': node['minimum'], 'source_maximum': node['maximum'],
                              'extrema_error_local': error})
    vertices = [(x - ORIGIN[0], y - ORIGIN[1], ((value - 32768) * quantum + bias) * scale[2] - ORIGIN[2])
                for x, y, value, nx, ny in samples]
    roundtrip = write_glb(out / 'TerrainMesh.glb', vertices, indices)
    encoded = json.dumps({'samples': samples, 'indices': indices}, separators=(',', ':')).encode()
    (out / 'TerrainSamples.json.gz').write_bytes(gzip.compress(encoded, mtime=0))
    report = {'result': 'source_height_samples_verified', 'source_probe': str(source_probe.relative_to(ROOT)),
              'source_probe_sha256': sha(source_probe / 'Parsed/Packages.json'),
              'source_region_cm': {'min': MINIMUM, 'max': MAXIMUM}, 'origin_cm': ORIGIN,
              'scale': scale, 'height_bias': bias, 'height_scale': height_scale,
              'height_decode': '(big_endian_rg16 - 32768) / (HeightScaleInLocalSpace * 128) + HeightBiasInLocalSpace',
              'height_quantization': 'Nonnegative encoded values truncated downward; audited against all selected source patch extrema.',
              'quantum_world_cm': quantum * scale[2], 'max_extrema_error_world_cm': max_extrema_error * scale[2],
              'header_nodes': len(nodes), 'header_tail': tail, 'patches': patch_reports,
              'tiles': tile_ids, 'vertices': len(vertices), 'triangles': len(indices) // 3,
              'matching_shared_samples': shared, 'glb_roundtrip_error_cm': roundtrip,
              'triangulation': 'Diagnostic fixed-diagonal grid; original engine LOD stitching and triangle-index generation have not been recovered.',
              'files': {name: {'sha256': sha(out / name), 'bytes': (out / name).stat().st_size}
                        for name in ['TerrainMesh.glb', 'TerrainSamples.json.gz']},
              'boundary': 'Original height samples only. Raw normal bytes retained without guessing their shading decode. Diagnostic GLB is not native map acceptance. Original triangulation, materials, UE import, collision and runtime acceptance remain pending.'}
    write_json(out / 'Report.json', report)
    print(json.dumps({k: v for k, v in report.items() if k not in {'patches', 'files'}}))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    parser.add_argument('--source-probe', type=Path, default=WORK / 'SourceRuns/Phase7BSource01')
    args = parser.parse_args()
    if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
        parser.error('Use a new alphanumeric run tag')
    recover(args.tag, args.source_probe.resolve())
