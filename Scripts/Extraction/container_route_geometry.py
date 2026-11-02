"""Pure geometry audit of vertical capsules against asset-derived yaw boxes.

No Unreal process, asset edit or route mutation is performed. A linear capsule
centre path is not a CharacterMovement sweep, jump trajectory or runtime proof.
All lengths are centimetres; capsule half-height includes its spherical caps.
"""
import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path


def _vector(value, label):
    if len(value) != 3:
        raise ValueError(label + ' must have three components')
    result = tuple(float(v) for v in value)
    if not all(math.isfinite(v) for v in result):
        raise ValueError(label + ' must be finite')
    return result


def _capsule(radius, half_height):
    radius, half_height = float(radius), float(half_height)
    if not (math.isfinite(radius) and math.isfinite(half_height)
            and 0 < radius <= half_height):
        raise ValueError('Capsule requires finite 0 < radius <= half_height')
    return radius, half_height


@dataclass(frozen=True)
class YawBox:
    identifier: str
    group: str
    center: tuple
    half_extent: tuple
    yaw_radians: float

    def __post_init__(self):
        object.__setattr__(self, 'center', _vector(self.center, 'Box center'))
        object.__setattr__(self, 'half_extent', _vector(self.half_extent, 'Box half extent'))
        if min(self.half_extent) < 0 or not math.isfinite(self.yaw_radians):
            raise ValueError('Box extents must be nonnegative and yaw finite')

    @classmethod
    def from_asset_point(cls, point):
        derived = point['derived_native']
        q = tuple(float(v) for v in derived['world_rotation_xyzw'])
        if (len(q) != 4 or not all(math.isfinite(v) for v in q)
                or abs(sum(v*v for v in q) - 1) > 1e-6):
            raise ValueError('Asset rotation must be a finite unit quaternion')
        if abs(q[0]) > 1e-8 or abs(q[1]) > 1e-8:
            raise ValueError('Only yaw-only boxes with a vertical Z axis are supported')
        return cls(point['id'], point['group'], derived['world_box_center_cm'],
                   derived['world_half_extent_cm'], math.atan2(2*q[2]*q[3], 1-2*q[2]*q[2]))

    def local(self, world):
        x, y, z = (world[i] - self.center[i] for i in range(3))
        c, s = math.cos(self.yaw_radians), math.sin(self.yaw_radians)
        return (c*x + s*y, -s*x + c*y, z)


@dataclass(frozen=True)
class EdgeDistance:
    axis_box_distance_cm: float
    clearance_cm: float
    parameter: float
    center_cm: tuple
    intersects: bool


def capsule_edge_distance(start_center, end_center, box, *, radius, half_height,
                          contact_tolerance=1e-7):
    """Exact minimum axis-to-OBB distance along the specified straight edge.

    The vertical capsule axis extends half_height-radius above/below its centre.
    Its distance to a yaw box is the point distance to that box expanded ONLY in
    Z by the axis half-length. Squared distance is piecewise quadratic; changes
    occur at the six local slab faces. Every interval endpoint and its stationary
    point are checked. Tangency counts as intersection (within the stated cm
    tolerance). Negative clearance is not an exact penetration depth.
    """
    start, end = _vector(start_center, 'Start center'), _vector(end_center, 'End center')
    radius, half_height = _capsule(radius, half_height)
    if not math.isfinite(contact_tolerance) or contact_tolerance < 0:
        raise ValueError('Contact tolerance must be finite and nonnegative')
    local = box.local(start)
    finish = box.local(end)
    velocity = tuple(finish[i] - local[i] for i in range(3))
    extent = (box.half_extent[0], box.half_extent[1],
              box.half_extent[2] + half_height - radius)
    breaks = {0., 1.}
    for origin, slope, width in zip(local, velocity, extent):
        if slope != 0:
            for face in (-width, width):
                t = (face - origin) / slope
                if 0 < t < 1:
                    breaks.add(t)
    breaks = sorted(breaks)

    def distance_squared(t):
        return sum(max(abs(origin + slope*t) - width, 0.)**2
                   for origin, slope, width in zip(local, velocity, extent))

    candidates = list(breaks)
    for left, right in zip(breaks, breaks[1:]):
        middle = (left + right) / 2
        quadratic = linear = 0.
        for origin, slope, width in zip(local, velocity, extent):
            value = origin + slope*middle
            if abs(value) > width:
                intercept = origin - math.copysign(width, value)
                quadratic += slope*slope
                linear += slope*intercept
        if quadratic > 0:
            candidates.append(max(left, min(right, -linear / quadratic)))
    minimum, parameter = min((distance_squared(t), t) for t in candidates)
    distance = math.sqrt(max(0., minimum))
    center = tuple(start[i] + (end[i] - start[i])*parameter for i in range(3))
    return EdgeDistance(distance, distance - radius, parameter, center,
                        distance <= radius + contact_tolerance)


def _broad_overlap(start, end, box, radius, half_height):
    """Conservative world AABB rejection only; never accepts an intersection."""
    c, s = abs(math.cos(box.yaw_radians)), abs(math.sin(box.yaw_radians))
    ex, ey, ez = box.half_extent
    expanded = (c*ex + s*ey + radius, s*ex + c*ey + radius, ez + half_height)
    return all(max(start[i], end[i]) >= box.center[i] - expanded[i] - 1e-7
               and min(start[i], end[i]) <= box.center[i] + expanded[i] + 1e-7
               for i in range(3))


def _sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def audit_route(asset_audit, route, snapshot):
    """Audit only route variants compatible with the snapshot's selected boxes.

    Spawn-pair variants remain alternatives, not a claim of observed traversal.
    Snapshot positions must agree with authored translations. Rotations/bounds
    come from the asset audit, since the live snapshot does not expose them.
    """
    if not (asset_audit.get('export_complete') is True
            and asset_audit.get('asset_files_unchanged') is True
            and asset_audit.get('result') in ('passed', 'passed_with_plan_difference')):
        raise ValueError('A complete, unchanged asset audit is required')
    cdo = asset_audit['cdo']
    if (any(abs(math.remainder(v, 360)) > 1e-6
            for v in cdo['container_relative_rotation_pitch_yaw_roll'])
            or any(abs(v-1) > 1e-6 for v in cdo['container_relative_scale3d'])):
        raise ValueError('Nonidentity ContainerBounds CDO rotation/scale is unsupported')
    points = {p['id']: p for p in asset_audit['points']}
    if len(points) != len(asset_audit['points']) or len(points) != asset_audit['candidate_count']:
        raise ValueError('Duplicate or incomplete asset candidate records')
    selected, seen_groups = {}, set()
    ignored_loot = 0
    for item in snapshot['loot']:
        identifier = item['source_point']
        if identifier not in points:
            if identifier.startswith('7F_'):
                raise ValueError('Unexported current 7F candidate: ' + identifier)
            ignored_loot += 1
            continue
        point = points[identifier]
        if (identifier in selected or point['group'] in seen_groups or item['loose']
                or not point['enabled'] or point['weight'] <= 0 or point['kind'].casefold() == 'loose'):
            raise ValueError('Invalid, duplicated or mutually exclusive selected candidate: ' + identifier)
        if math.dist(item['position'], point['transform']['translation_cm']) > .05:
            raise ValueError('Snapshot position differs from the asset candidate: ' + identifier)
        selected[identifier] = YawBox.from_asset_point(point)
        seen_groups.add(point['group'])
    expected_groups = {p['group'] for p in points.values() if p['enabled'] and p['weight'] > 0}
    if not selected or seen_groups != expected_groups:
        raise ValueError('Snapshot is missing selected current 7F groups')
    players = {p['name']: p for p in snapshot['players']}
    capsules = {}
    for role in ('A', 'B'):
        player = players['EX_' + role]
        radius, half = _capsule(player['capsule_radius'], player['capsule_half'])
        capsules[role] = dict(radius_cm=radius, half_height_cm=half)

    def compatible(identifier):
        node = route['nodes'][identifier]
        if node['kind'] == 'peer_parking':
            return compatible(node['parent'])
        return node['kind'] != 'loot' or identifier in selected

    summary = dict(route_pairs=len(route['pair_legs']), compatible_pairs=0, skipped_pairs=0,
                   path_edges=0, box_edge_checks=0, broad_phase_rejections=0,
                   exact_distance_checks=0, intersecting_box_edges=0)
    conflicts = []
    for pair in route['pair_legs']:
        if not all(compatible(pair[k]) for k in ('a_start', 'b_start', 'destination', 'b_end')):
            summary['skipped_pairs'] += 1
            continue
        summary['compatible_pairs'] += 1
        for role in ('A', 'B'):
            radius, half = capsules[role]['radius_cm'], capsules[role]['half_height_cm']
            feet = [_vector(p, 'Measured route foot point') for p in pair[role]['points']]
            if not feet:
                raise ValueError('Route path has no measured points')
            centers = [(p[0], p[1], p[2] + half) for p in feet]
            edges = list(zip(centers, centers[1:])) or [(centers[0], centers[0])]
            summary['path_edges'] += len(edges)
            for box in selected.values():
                hit_count, worst = 0, None
                for index, (start, end) in enumerate(edges):
                    summary['box_edge_checks'] += 1
                    if not _broad_overlap(start, end, box, radius, half):
                        summary['broad_phase_rejections'] += 1
                        continue
                    summary['exact_distance_checks'] += 1
                    result = capsule_edge_distance(start, end, box, radius=radius, half_height=half)
                    if not result.intersects:
                        continue
                    hit_count += 1
                    summary['intersecting_box_edges'] += 1
                    if worst is None or result.clearance_cm < worst['clearance_cm']:
                        worst = dict(edge_index=index, parameter=result.parameter,
                                     axis_box_distance_cm=result.axis_box_distance_cm,
                                     clearance_cm=result.clearance_cm, center_cm=result.center_cm,
                                     start_feet_cm=feet[index], end_feet_cm=feet[min(index+1, len(feet)-1)])
                if hit_count:
                    conflicts.append(dict(a_start=pair['a_start'], b_start=pair['b_start'],
                                          destination=pair['destination'], b_end=pair['b_end'], role=role,
                                          container=box.identifier, group=box.group,
                                          intersecting_edges=hit_count, closest=worst))
    return dict(schema=1, result='completed', runtime_proof=False,
                classification='ideal_centerline_intersections' if conflicts else 'no_ideal_centerline_intersections',
                scope='Selected 7F asset-derived yaw OBBs vs linear, vertical capsule centre paths from measured feet; '
                      'not an Unreal capsule Sweep, observed motion, jump arc or runtime traversal proof.',
                assumptions=['Grounded capsule center = measured feet + snapshot half-height in world Z.',
                             'Boxes retain audited asset transforms; snapshot verifies selection and translation only.',
                             'Only actual exported 7F candidates are covered; other scenery, loot and players are not.',
                             'Compatible spawn-pair alternatives are audited, not identified as actually walked.',
                             'Negative clearance is axis-to-box distance minus radius, not penetration depth.',
                             'CDO NO_COLLISION precedes native OnRep_Appearance enabling QueryOnly/Pawn Block.'],
                selected_containers=sorted(selected), ignored_nonexported_loot=ignored_loot,
                capsules_from_snapshot=capsules, contact_tolerance_cm=1e-7,
                summary=summary, conflicts=conflicts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--asset-audit', type=Path, required=True)
    parser.add_argument('--route', type=Path, required=True)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--expected-route-sha256')
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    if not output.is_relative_to(root / 'Saved') or output.exists():
        parser.error('--output must be a new file under the project Saved directory')
    if args.expected_route_sha256 and _sha(args.route) != args.expected_route_sha256:
        parser.error('Route hash does not match the requested audited version')
    asset_audit = _json(args.asset_audit)
    changed = [name for name, digest in asset_audit['input_sha256'].items() if _sha(root / name) != digest]
    if changed:
        parser.error('Asset-audit provenance changed: ' + ', '.join(changed))
    inputs = [args.asset_audit, args.route, args.snapshot, Path(__file__).resolve()]
    hashes = {str(p.resolve()): _sha(p) for p in inputs}
    report = audit_route(asset_audit, _json(args.route), _json(args.snapshot))
    if any(_sha(Path(p)) != digest for p, digest in hashes.items()):
        parser.error('Audit inputs changed during the read-only calculation')
    report['input_sha256'] = hashes
    report['asset_audit_provenance_verified'] = True
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(result=report['result'], classification=report['classification'],
                          summary=report['summary'], output=str(output)), ensure_ascii=False))


if __name__ == '__main__':
    main()
