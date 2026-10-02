"""Validate exported GLB triangles and centimetre/axis conversion against source bounds."""
import json
import math
import struct
from pathlib import Path
from source_packages import WORK, write_json


def glb(path):
    data = Path(path).read_bytes()
    magic, version, size = struct.unpack_from('<III', data)
    assert magic == 0x46546C67 and version == 2 and size == len(data)
    length, kind = struct.unpack_from('<II', data, 12)
    assert kind == 0x4E4F534A
    doc = json.loads(data[20:20 + length])
    offset = 20 + length
    length, kind = struct.unpack_from('<II', data, offset)
    assert kind == 0x004E4942
    return doc, data[offset + 8:offset + 8 + length]


def accessor(doc, binary, index):
    a = doc['accessors'][index]
    view = doc['bufferViews'][a['bufferView']]
    fmt = {5121: 'B', 5123: 'H', 5125: 'I', 5126: 'f'}[a['componentType']]
    width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
    size = struct.calcsize('<' + fmt * width)
    stride = view.get('byteStride', size)
    start = view.get('byteOffset', 0) + a.get('byteOffset', 0)
    return [struct.unpack_from('<' + fmt * width, binary, start + i * stride) for i in range(a['count'])]


def audit():
    manifest = json.loads((WORK / 'ImportManifest.json').read_text(encoding='utf-8'))
    checks = []
    for path, entry in manifest['meshes'].items():
        try:
            doc, binary = glb(entry['file'])
            points, triangles = [], 0
            for mesh in doc['meshes']:
                for primitive in mesh['primitives']:
                    assert primitive.get('mode', 4) == 4, 'non-triangle primitive'
                    vertices = accessor(doc, binary, primitive['attributes']['POSITION'])
                    indices = accessor(doc, binary, primitive['indices'])
                    assert len(indices) % 3 == 0 and len(indices) > 0
                    assert all(0 <= i[0] < len(vertices) for i in indices), 'index outside vertex buffer'
                    assert all(all(math.isfinite(c) for c in v) for v in vertices), 'non-finite vertex'
                    points.extend((v[0] * 100, v[2] * 100, v[1] * 100) for v in vertices)
                    triangles += len(indices) // 3
            assert points and triangles
            lower = [min(v[i] for v in points) for i in range(3)]
            upper = [max(v[i] for v in points) for i in range(3)]
            bounds = entry['metadata']['bounds']
            expected_low = [bounds['Origin'][k] - bounds['BoxExtent'][k] for k in ['X', 'Y', 'Z']]
            expected_high = [bounds['Origin'][k] + bounds['BoxExtent'][k] for k in ['X', 'Y', 'Z']]
            outside = max(max(expected_low[i] - lower[i], upper[i] - expected_high[i]) for i in range(3))
            assert outside <= 1., 'export exceeds original bound by ' + str(outside) + ' cm'
            checks.append({'source': path, 'passed': True, 'vertices': len(points), 'triangles': triangles,
                           'min_cm': lower, 'max_cm': upper, 'source_bound_excess_cm': outside,
                           'recovery': entry['metadata'].get('recovery')})
        except Exception as error:
            checks.append({'source': path, 'passed': False, 'error': str(error)})
    report = {'result': 'passed' if checks and all(x['passed'] for x in checks) else 'failed', 'checks': checks}
    write_json(WORK / 'GeometryAudit.json', report)
    print(report['result'], len(checks), [c for c in checks if not c['passed']])
    return 0 if report['result'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(audit())
