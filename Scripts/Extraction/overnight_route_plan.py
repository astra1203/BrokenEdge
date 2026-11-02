"""Build the overnight route from measured floors, never from straight lines.

Run with the engine's bundled Python. This does not open Unreal or modify assets.
The resulting graph paths are planning evidence; real input walking is still required.
"""
import argparse
import hashlib
import heapq
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts/SpaceCenter'))
from plan_route import FloorGraph
from plan_phase7f_route import RUNTIME_AVOIDANCE, avoid, block_ledges

PLAN = 'Docs/Extraction/RestorationEvidence/Phase7FGameplay/Plan.json'
ANCHORS = 'Docs/Extraction/RestorationEvidence/SpawnExit/OfficialSpawnExitAnchors.json'
MAP = 'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap'
GROUPS = ['7F_WestDorm_A', '7F_WestDorm_B', '7F_LivingArea_B']
GUARD_ROUTE = 'ExtractionPatrol_7F_WestDorm'
RESERVED_RADIUS = 180.0
# A local planning exclusion, not a change to character collision or driver
# arrival tolerance. Z refers to measured feet/floor height, not capsule centre.
OVERNIGHT_RUNTIME_AVOIDANCE = [{
    'id': 'overnight-route01-dorm-low-seam',
    'center': [-19286.49, -19834.27, 194.92],
    'radius_cm': 180.0,
    'floor_z_window_cm': [100.0, 280.0],
    'reason': 'Input-driven walking blocked on SpaceCenterInstanceGroup_12930/Instances; '
              'accel=2048, speed=0.2, normalZ=-0.30, BlockAll; raised probe was clear.',
    'evidence': {
        'run': 'overnight-route01',
        'log_lines': [1351, 1355],
        'sha256': {
            'Saved/Extraction/overnight-route01/A.log':
                'b2aad35d6668404406eaf26411514325d631b4976e861c4145f0e0f91e5fb07e',
            'Saved/Extraction/overnight-route01/Report.json':
                'dbcc3d8c8910f490cd3a91eb2e780dcdb7d9bf5583311a9355416c59ab91ad17',
            'Saved/Extraction/overnight-route01/OvernightMotion.jsonl':
                '98b771596a75d0fbe0c754a6e612c51715a3af97164570425ed54adbdc9dde38',
            'Saved/Extraction/overnight-route01/OvernightRoute.json':
                '99ad43ee8617f73faa6471f18970d2beb915da7228b280e45e24fa2cd76b4ae3',
        },
    },
}, {
    'id': 'overnight-route-flight01-living-area-gear-crate',
    'center': [-16300.0, -18000.0, 196.0],
    'radius_cm': 180.0,
    'floor_z_window_cm': [100.0, 280.0],
    'source_point': '7F_LivingArea_A2',
    'reason': 'Input-driven walking through the live gear crate was blocked by ContainerBounds; '
              'accel=2048, speed=2.3; four trace heights blocked and raised45 sweepBlocked=1.',
    'evidence': {
        'run': 'overnight-route-flight01',
        'log_lines': [1355, 1363],
        'container_id': '8E81789944EDEB6BCC17F28EDFE7FD10',
        'sha256': {
            'Saved/Extraction/overnight-route-flight01/A.log':
                '761c84578bd44da522f79b78cd22a13789990922bc66dd7dcdf753623ef3f71e',
            'Saved/Extraction/overnight-route-flight01/Report.json':
                'da6e03baae44ef07fe9d6c7dde1f4a3d7c517d76c19333716d742895bdc707a5',
            'Saved/Extraction/overnight-route-flight01/OvernightMotion.jsonl':
                'f306f9737c5d03f6854493982cfa6631abb491ff3cfaeb89bfa899be495750b1',
            'Saved/Extraction/overnight-route-flight01/OvernightRoute.json':
                '653e1751eadc316291d58c17335346db540031f71a004342bfc03f71defd94b6',
        },
    },
}]


def segment_distance_2d(a, b, point):
    dx, dy = b[0] - a[0], b[1] - a[1]
    denominator = dx * dx + dy * dy
    t = max(0., min(1., ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / denominator)) if denominator else 0.
    return math.hypot(a[0] + t * dx - point[0], a[1] + t * dy - point[1])


def edge_avoids_stationary(a, b, stationary):
    # A 180 cm centre-line reserve covers both character radii plus the driver's
    # endpoint/waypoint tolerance. Different floors over 2 m apart do not clash.
    return (min(abs(a[2] - stationary[2]), abs(b[2] - stationary[2])) > 200
            or segment_distance_2d(a, b, stationary) >= RESERVED_RADIUS)


def edge_avoids_runtime(a, b, zones):
    """Check the whole segment against each closed, same-floor cylinder."""
    for zone in zones:
        low, high = zone['floor_z_window_cm']
        assert low <= high and zone['radius_cm'] > 0
        dz = b[2] - a[2]
        if abs(dz) < 1e-9:
            if not low <= a[2] <= high:
                continue
            first, last = 0., 1.
        else:
            first, last = sorted(((low - a[2]) / dz, (high - a[2]) / dz))
            first, last = max(0., first), min(1., last)
            if first > last:
                continue
        # Clipping Z first avoids rejecting a different floor merely because
        # its XY projection crosses the exclusion.
        clipped = [[a[i] + t * (b[i] - a[i]) for i in range(3)] for t in (first, last)]
        if segment_distance_2d(*clipped, zone['center']) <= zone['radius_cm']:
            return False
    return True


def apply_runtime_avoidance(graph, zones):
    removed = [key for key, point in graph.nodes.items()
               if not edge_avoids_runtime(point['location'], point['location'], zones)]
    for key in removed:
        del graph.nodes[key]
        graph.cells[key[:2]].remove(key)
    previous_edges = len(graph.blocked)
    for key, point in graph.nodes.items():
        a = point['location']
        # FloorGraph only connects adjacent 100 cm cells. Restrict the work to
        # nearby cells, then use the exact segment test (not endpoint-only).
        if not any(abs(a[0] - z['center'][0]) <= z['radius_cm'] + 101
                   and abs(a[1] - z['center'][1]) <= z['radius_cm'] + 101 for z in zones):
            continue
        for other in list(graph.adjacent(key)):
            if not edge_avoids_runtime(a, graph.nodes[other]['location'], zones):
                graph.blocked.add(frozenset((key, other)))
    return dict(removed_nodes=len(removed), blocked_edges=len(graph.blocked) - previous_edges)


def reserved_path(graph, start, end, stationary):
    """Measured edges with the other player's stationary capsule reserved."""
    key_for = lambda p: (round(p[0] / 100), round(p[1] / 100), round(p[2] / 5))
    first, last = key_for(start), key_for(end)
    assert first in graph.nodes and last in graph.nodes
    if not edge_avoids_stationary(start, start, stationary) or not edge_avoids_stationary(end, end, stationary):
        raise ValueError('Endpoint conflicts with stationary teammate')
    # Peer alternatives repeatedly ask about the same fixed obstruction. A
    # failed search has already measured a disconnected component; remember
    # the smaller side of that partition instead of exhausting the whole graph
    # for every subsequent endpoint. Both sides remain subject to real edges.
    if not hasattr(graph, '_reserved_paths'):
        graph._reserved_paths, graph._reserved_partitions = {}, {}
    obstacle = tuple(stationary)
    cache_key = (first, last, obstacle)
    if cache_key in graph._reserved_paths:
        return [graph.nodes[key] for key in graph._reserved_paths[cache_key]]
    if any((first in partition) != (last in partition)
           for partition in graph._reserved_partitions.get(obstacle, [])):
        raise ValueError('No measured route avoiding the stationary teammate (cached disconnected component)')
    queue, costs, parents = [(0., first)], {first: 0.}, {first: None}
    expanded = set()
    while queue:
        _, node = heapq.heappop(queue)
        if node in expanded:
            continue
        expanded.add(node)
        if node == last:
            result = []
            while node is not None:
                result.append(node)
                node = parents[node]
            graph._reserved_paths[cache_key] = result[::-1]
            return [graph.nodes[key] for key in graph._reserved_paths[cache_key]]
        for other in graph.adjacent(node):
            a, b = graph.nodes[node]['location'], graph.nodes[other]['location']
            if not edge_avoids_stationary(a, b, stationary):
                continue
            cost = costs[node] + math.dist(a, b)
            if cost < costs.get(other, math.inf):
                costs[other], parents[other] = cost, node
                heapq.heappush(queue, (cost + math.dist(b, end), other))
    partition = set(costs) if len(costs) <= len(graph.nodes) / 2 else graph.nodes.keys() - costs.keys()
    graph._reserved_partitions.setdefault(obstacle, []).append(partition)
    raise ValueError('No measured route avoiding the stationary teammate')


def packed_path(path):
    return dict(points=[[round(v, 4) for v in p['location']] for p in path],
                length_cm=round(sum(math.dist(a['location'], b['location']) for a, b in zip(path, path[1:])), 2),
                floor_actors=sorted({p['actor'] for p in path}))


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def build(root=ROOT, probe_directory=None):
    plan, anchors = load(root / PLAN), load(root / ANCHORS)
    probe_dir = root / (probe_directory or plan['source_probe'])
    probe_reference = probe_dir.resolve().relative_to(root).as_posix()
    print('Loading measured floor graph', flush=True)
    graph = FloorGraph(load(probe_dir / 'Probes.json'), load(probe_dir / 'BlockedEdges.json'))
    avoid(graph, RUNTIME_AVOIDANCE)
    block_ledges(graph)
    for zone in OVERNIGHT_RUNTIME_AVOIDANCE:
        for name, expected in zone['evidence']['sha256'].items():
            assert digest(root / name) == expected, 'Runtime avoidance evidence changed: ' + name
    exclusions = apply_runtime_avoidance(graph, OVERNIGHT_RUNTIME_AVOIDANCE)
    print('Evidence-based runtime exclusions:', exclusions, flush=True)
    print('Measured clear nodes:', len(graph.nodes), flush=True)
    spawn = next(g for g in anchors['spawn_groups'] if g['id'] == 'OfficialWestDorm')
    exit_row = next(e for e in anchors['exits'] if e['id'] == 'OfficialExitSquare')
    guard = next(r for r in plan['routes'] if r['name'] == GUARD_ROUTE)
    definitions = []
    for p in spawn['points']:
        definitions.append(dict(id=p['actor'], kind='spawn', source=p['world']))
    for group in GROUPS:
        for p in plan['containers']:
            if p['group'] == group:
                definitions.append(dict(id=p['id'], group=group, kind='loot', recipe=p['recipe'], source=p['location']))
    definitions += [dict(id=GUARD_ROUTE, kind='encounter', source=guard['points'][0]),
                    dict(id='OfficialExitSquare', kind='exit', source=exit_row['world'])]
    nodes, problems = {}, []
    for row in definitions:
        point = row['source']
        # Stop beside a crate, not at the centre occupied by its collision.
        if row['kind'] == 'loot':
            candidates = [(key, p) for key, p in graph.nodes.items()
                          if 100 <= math.dist(point[:2], p['location'][:2]) <= 160
                          and abs(point[2] - p['location'][2]) < 60]
            if not candidates:
                problems.append(row['id'] + ': no measured same-floor approach within interaction range')
                continue
            key, measured = min(candidates, key=lambda pair: math.dist(point, pair[1]['location']))
        else:
            key = graph.near(point)
            measured = graph.nodes[key]
            if math.dist(point, measured['location']) > 175:
                problems.append(row['id'] + ': nearest measured floor is more than 175 cm away')
                continue
        nodes[row['id']] = dict(row, position=measured['location'], floor_actor=measured['actor'],
                               approach_offset_cm=round(math.dist(point, measured['location']), 2))
    stages = [[p['actor'] for p in spawn['points']]]
    stages += [[p['id'] for p in plan['containers'] if p['group'] == group] for group in GROUPS]
    stages += [[GUARD_ROUTE], ['OfficialExitSquare']]
    legs = []
    for starts, ends in zip(stages, stages[1:]):
        for start in starts:
            for end in ends:
                if start not in nodes or end not in nodes:
                    continue
                try:
                    path = graph.path(nodes[start]['position'], nodes[end]['position'])
                except ValueError as exc:
                    problems.append(start + ' -> ' + end + ': ' + str(exc))
                    continue
                length = sum(math.dist(a['location'], b['location']) for a, b in zip(path, path[1:]))
                legs.append(dict(start=start, end=end, length_cm=round(length, 2),
                                 points=[[round(v, 4) for v in p['location']] for p in path],
                                 floor_actors=sorted({p['actor'] for p in path})))
                print(start, '->', end, len(path), 'points,', round(length / 100, 1), 'm', flush=True)
    # Each destination has a separately measured peer parking node. Movement is
    # sequential: A avoids B's actual previous node, then B avoids A's endpoint.
    # Never truncate B's path and claim B arrived at A's occupied endpoint.
    primary_nodes = list(nodes.values())
    peer_options = {}
    for row in primary_nodes:
        if row['kind'] == 'spawn':
            continue
        point = row['position']
        candidates = []
        for key, measured in graph.nodes.items():
            p = measured['location']
            distance = math.dist(point[:2], p[:2])
            # The guard's source patrol point is on a sloped transition. Its
            # peer may wait on a connected adjacent landing, never a guessed Z.
            if abs(point[2] - p[2]) > (190 if row['kind'] == 'encounter' else 70):
                continue
            if row['kind'] == 'exit':
                # Stay safely inside the existing 250 cm half-extent exit box.
                yaw = math.radians(exit_row.get('yaw', 0))
                dx, dy = p[0] - row['source'][0], p[1] - row['source'][1]
                local = (dx * math.cos(yaw) + dy * math.sin(yaw), -dx * math.sin(yaw) + dy * math.cos(yaw))
                if not (190 <= distance <= 340 and max(abs(v) for v in local) <= 180):
                    continue
            elif not 400 <= distance <= (1200 if row['kind'] == 'encounter' else 700):
                continue
            # Do not park a character on another loot container or any primary
            # interaction endpoint; the next A leg must have room to start.
            if any(math.dist(p[:2], box['location'][:2]) < 180 and abs(p[2] - box['location'][2]) < 200
                   for box in plan['containers']):
                continue
            if any(math.dist(p[:2], n['position'][:2]) < 190 and abs(p[2] - n['position'][2]) < 200
                   for n in primary_nodes):
                continue
            candidates.append((abs(distance - (250 if row['kind'] == 'exit' else 500)), measured))
        parking = None
        peer_options[row['id']] = [measured for _, measured in sorted(candidates, key=lambda pair: pair[0])[:40]]
        for measured in peer_options[row['id']]:
            try:
                graph.path(point, measured['location'])
                parking = measured
                break
            except ValueError:
                continue
        if parking is None:
            problems.append(row['id'] + ': no measured peer parking point')
            continue
        ident = row['id'] + '__peer'
        nodes[ident] = dict(id=ident, kind='peer_parking', parent=row['id'], position=parking['location'],
                            source=parking['location'], floor_actor=parking['actor'])
    # A crate exclusion can leave a connected single-player corridor whose
    # only exit is blocked by a nominal peer parking point. Check both entry
    # and departure with the actual stationary capsule before accepting it.
    def usable_peer(ident, candidate):
        stage = next(i for i, names in enumerate(stages) if ident in names)
        previous = stages[stage - 1]
        incoming_peers = previous if stage == 1 else [name + '__peer' for name in previous]
        checks = [(nodes[name]['position'], candidate, nodes[ident]['position']) for name in incoming_peers]
        if stage + 1 < len(stages):
            for end in stages[stage + 1]:
                checks += [(nodes[ident]['position'], nodes[end]['position'], candidate),
                           (candidate, nodes[end + '__peer']['position'], nodes[end]['position'])]
        try:
            for start, end, stationary in checks:
                reserved_path(graph, start, end, stationary)
        except ValueError:
            return False
        return True

    for _ in range(3):
        changed = False
        for row in primary_nodes:
            ident = row['id'] + '__peer'
            if row['kind'] == 'spawn' or ident not in nodes:
                continue
            if usable_peer(row['id'], nodes[ident]['position']):
                continue
            for candidate in peer_options[row['id']]:
                if not usable_peer(row['id'], candidate['location']):
                    continue
                print('Repark measured peer:', ident, nodes[ident]['position'], '->', candidate['location'], flush=True)
                nodes[ident].update(position=candidate['location'], source=candidate['location'], floor_actor=candidate['actor'])
                changed = True
                break
        if not changed:
            break
    pair_legs = []
    for index, (starts, ends) in enumerate(zip(stages, stages[1:])):
        combinations = [(a, b) for a in starts for b in starts if a != b] if index == 0 else [(a, a + '__peer') for a in starts]
        for a, b in combinations:
            for destination in ends:
                peer = destination + '__peer'
                if any(n not in nodes for n in (a, b, destination, peer)):
                    continue
                try:
                    first = reserved_path(graph, nodes[a]['position'], nodes[destination]['position'], nodes[b]['position'])
                    second = reserved_path(graph, nodes[b]['position'], nodes[peer]['position'], nodes[destination]['position'])
                except ValueError as exc:
                    problems.append('pair ' + a + '/' + b + ' -> ' + destination + ': ' + str(exc))
                    continue
                pair_legs.append(dict(a_start=a, b_start=b, destination=destination, b_end=peer,
                                      order=['A', 'B'], reserved_radius_cm=RESERVED_RADIUS,
                                      A=packed_path(first), B=packed_path(second)))
    print('Sequential collision-reserved pair legs:', len(pair_legs), flush=True)
    inputs = [PLAN, ANCHORS, MAP, 'Scripts/Extraction/overnight_route_plan.py', 'Scripts/SpaceCenter/plan_route.py',
              'Scripts/SpaceCenter/plan_phase7f_route.py',
              probe_reference + '/Probes.json', probe_reference + '/BlockedEdges.json']
    return dict(schema=1, status='blocked' if problems else 'graph_connected_requires_runtime_validation',
                boundary='Historical floor probes with ledge and known runtime avoidance filters; not a walking pass.',
                map=MAP, spawn_group=spawn['id'], search_groups=GROUPS, guard_route=GUARD_ROUTE,
                exit='OfficialExitSquare', exit_rule=dict(opens=900, closes=1020, mechanism='project timed compatibility'),
                planned_map_sha256=digest(root / MAP), source_probe=probe_reference,
                input_sha256={name: digest(root / name) for name in inputs},
                runtime_avoidance=OVERNIGHT_RUNTIME_AVOIDANCE, runtime_exclusion_counts=exclusions,
                stages=stages, nodes=nodes, legs=legs, pair_legs=pair_legs, problems=problems)


def validate(route):
    errors = list(route['problems'])
    zones = route.get('runtime_avoidance', OVERNIGHT_RUNTIME_AVOIDANCE)

    def avoids_runtime(points):
        return (all(edge_avoids_runtime(point, point, zones) for point in points)
                and all(edge_avoids_runtime(a, b, zones) for a, b in zip(points, points[1:])))

    legs = {(p['start'], p['end']): p for p in route['legs']}
    for starts, ends in zip(route['stages'], route['stages'][1:]):
        for start in starts:
            for end in ends:
                if (start, end) not in legs:
                    errors.append('Missing alternative ' + start + ' -> ' + end)
    for leg in route['legs']:
        points = leg['points']
        if not points or any(not all(math.isfinite(v) for v in p) for p in points):
            errors.append('Invalid points ' + leg['start'] + ' -> ' + leg['end'])
            continue
        if not avoids_runtime(points):
            errors.append('Known runtime blocker ' + leg['start'] + ' -> ' + leg['end'])
        for a, b in zip(points, points[1:]):
            # FloorGraph is a four-neighbour 100 cm grid, with small source float noise.
            if math.dist(a[:2], b[:2]) > 101 or abs(a[2] - b[2]) >= 80.01:
                errors.append('Unmeasured jump ' + leg['start'] + ' -> ' + leg['end'])
                break
    pairs = {(p['a_start'], p['b_start'], p['destination']): p for p in route.get('pair_legs', [])}
    for index, (starts, ends) in enumerate(zip(route['stages'], route['stages'][1:])):
        combinations = [(a, b) for a in starts for b in starts if a != b] if index == 0 else [(a, a + '__peer') for a in starts]
        for a, b in combinations:
            for end in ends:
                if (a, b, end) not in pairs:
                    errors.append('Missing safe pair ' + a + '/' + b + ' -> ' + end)
    for pair in route.get('pair_legs', []):
        if pair['order'] != ['A', 'B']:
            errors.append('Concurrent peer movement is not allowed')
        for role, stationary in [('A', pair['b_start']), ('B', pair['destination'])]:
            points = pair[role]['points']
            fixed = route['nodes'][stationary]['position']
            if not avoids_runtime(points):
                errors.append('Known runtime blocker in pair ' + pair['a_start'] + ' -> ' + pair['destination'] + ' ' + role)
            if not points or any(not edge_avoids_stationary(a, b, fixed) for a, b in zip(points, points[1:])):
                errors.append('Peer collision corridor ' + pair['a_start'] + ' -> ' + pair['destination'] + ' ' + role)
    return errors


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).with_name('overnight_route.json'))
    parser.add_argument('--validate-only', action='store_true')
    parser.add_argument('--probe-directory', type=Path, help='A completed merged probe directory under this project')
    args = parser.parse_args()
    result = load(args.output) if args.validate_only else build(probe_directory=args.probe_directory)
    errors = validate(result)
    if not args.validate_only:
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status=result['status'], legs=len(result['legs']), errors=errors), ensure_ascii=False))
    sys.exit(bool(errors))
