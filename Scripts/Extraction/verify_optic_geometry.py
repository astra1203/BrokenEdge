"""Independent ray/triangle check against an ASCII FBX export of the actual AK.

FBX export uses (X,-Y,Z); the original GLB lens uses (X,Z,Y), metres.
This nominal-pose check complements rendered captures, not animated-pose proof.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def scale(a, s): return tuple(x*s for x in a)
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def rotate(q, v):
    t = scale(cross(q[:3], v), 2)
    return add(v, add(scale(t, q[3]), cross(q[:3], t)))


def transform(t, p):
    return add(rotate(t['rotation_xyzw'], tuple(x*s for x, s in zip(p, t['scale']))), t['translation'])


def intersection(origin, target, triangle):
    direction = sub(target, origin)
    a, b, c = triangle
    e1, e2 = sub(b, a), sub(c, a)
    h = cross(direction, e2)
    determinant = dot(e1, h)
    if abs(determinant) < 1e-9: return None
    s = sub(origin, a)
    u = dot(s, h)/determinant
    if u < 0 or u > 1: return None
    q = cross(s, e1)
    v = dot(direction, q)/determinant
    if v < 0 or u+v > 1: return None
    t = dot(e2, q)/determinant
    return t if 0 < t < 1 else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--weapon-fbx', type=Path, required=True)
    parser.add_argument('--lens-glb', type=Path, required=True)
    parser.add_argument('--calibration', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    fbx = args.weapon_fbx.read_text(encoding='utf-8')
    vertices = list(map(float, re.search(r'Vertices: \*\d+\s*\{\s*a: ([^}]+)', fbx)[1].strip().split(',')))
    vertices = list(zip(vertices[::3], (-y for y in vertices[1::3]), vertices[2::3]))
    indices = list(map(int, re.search(r'PolygonVertexIndex: \*\d+\s*\{\s*a: ([^}]+)', fbx)[1].strip().split(',')))
    triangles, polygon = [], []
    for index in indices:
        polygon.append(index if index >= 0 else -index-1)
        if index < 0:
            triangles.extend(tuple(vertices[k] for k in (polygon[0], polygon[i], polygon[i+1])) for i in range(1, len(polygon)-1))
            polygon = []
    assert not polygon
    data = args.lens_glb.read_bytes()
    size = struct.unpack_from('<I', data, 12)[0]
    gltf = json.loads(data[20:20+size])
    primitive = next(p for m in gltf['meshes'] for p in m['primitives']
                     if gltf['materials'][p['material']]['name'].endswith('_glass'))
    bounds = gltf['accessors'][primitive['attributes']['POSITION']]
    low, high = [tuple(v[i]*100 for i in (0, 2, 1)) for v in (bounds['min'], bounds['max'])]
    calibration = json.loads(args.calibration.read_text(encoding='utf-8'))
    aim = calibration['aim']
    corrected = calibration['after']
    samples = []
    for x in range(5):
        for z in range(5):
            # Central 60 percent of the transparent aperture; rounded outer corners excluded.
            samples.append((low[0]+(high[0]-low[0])*(.2+.15*x), (low[1]+high[1])/2,
                            low[2]+(high[2]-low[2])*(.2+.15*z)))
    results = {}
    for name, dz in [('previous', -3.0), ('corrected', 0.0)]:
        mount = dict(corrected['first_person_mount'])
        mount['translation'] = add(mount['translation'], (0, 0, dz))
        center = transform(aim, transform(mount, tuple((a+b)/2 for a, b in zip(low, high))))
        translation = add(aim['translation'], (0, -center[1], -center[2]))
        q = aim['rotation_xyzw']
        eye = rotate([-q[0], -q[1], -q[2], q[3]], scale(translation, -1))
        blocked = sum(any(intersection(eye, transform(mount, point), triangle) is not None
                          for triangle in triangles) for point in samples)
        results[name] = {'eye_in_weapon_cm': eye, 'blocked_samples': blocked, 'total_samples': len(samples)}
    roof = max(v[2] for v in vertices if abs(v[0]) < 1.5 and 9 <= v[1] <= 15)
    report = {'method': __doc__, 'weapon_vertices': len(vertices), 'weapon_triangles': len(triangles),
              'receiver_roof_cm': roof, 'weapon_highest_cm': max(v[2] for v in vertices),
              'lens_bounds_cm': [low, high], 'rays': results,
              'input_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in [args.weapon_fbx, args.lens_glb, args.calibration]}}
    report['passed'] = results['previous']['blocked_samples'] > 0 and results['corrected']['blocked_samples'] == 0
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
