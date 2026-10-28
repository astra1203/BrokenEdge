"""Build the 7F regional walking route from measured capsule-clear floors."""
import argparse
import json
import math
from pathlib import Path

from plan_route import FloorGraph

# Visit order keeps each leg short; the integrate gate requires the control,
# launch and region_return checkpoints, the rest cover the 7F additions.
ORDER = ['west_gate', 'west_dorm', 'staff_passage_1', 'control_entry', 'control_upper', 'staff_passage_2',
         'east_print', 'east_assembly', 'east_canning', 'test_range', 'launch_arrival', 'launch_platform']
# Floors the probe measured as clear but where input-driven walking stalled.
RUNTIME_AVOIDANCE = [
    {'center': [1300, -2100, 363], 'radius_cm': 180,
     'evidence': 'Phase7EControlWalk02, Phase7FWalkWest01, Phase7FWalkEast01'},
    {'center': [3150, -12790, 445], 'radius_cm': 150, 'evidence': 'Phase7FWalkEast02'},
    {'center': [11100, -12250, 44], 'radius_cm': 150, 'evidence': 'Phase7FWalkEast03'},
]
# An isolated flat-to-flat rise this high is a ledge the character cannot step
# onto. Stair treads sampled every 100 cm rise just as much, but the rise
# continues on the next sample in the same direction; ramps and terrain keep
# the planner's wider 80 cm sample tolerance. A rise across two different
# meshes is a seam ledge even when one sample lands on a tilted lip.
LEDGE_CM = 45
STAIR_CONTINUES_CM = 30


def block_ledges(graph):
    flat = lambda key: graph.nodes[key]['normal'][2] > .98
    z = lambda key: graph.nodes[key]['location'][2]

    def continues(key, step, rise):
        cell = (key[0] + step[0], key[1] + step[1])
        # Stairs keep a steady rise per sample; a ledge beside some other
        # floor level does not.
        return any(rise * (z(o) - z(key)) >= STAIR_CONTINUES_CM * abs(rise)
                   and abs(z(o) - z(key) - rise) < 25 for o in graph.cells[cell])

    for key in list(graph.nodes):
        for other in list(graph.adjacent(key)):
            rise = z(other) - z(key)
            seam = graph.nodes[key]['actor'] != graph.nodes[other]['actor']
            if abs(rise) < LEDGE_CM or not (seam or (flat(key) and flat(other))):
                continue
            step = (other[0] - key[0], other[1] - key[1])
            if not (continues(other, step, rise) or continues(key, (-step[0], -step[1]), -rise)):
                graph.blocked.add(frozenset((key, other)))


def avoid(graph, zones):
    for zone in zones:
        for key in [k for k, p in graph.nodes.items()
                    if math.dist(p['location'][:2], zone['center'][:2]) < zone['radius_cm']
                    and abs(p['location'][2] - zone['center'][2]) < 150]:
            del graph.nodes[key]
            graph.cells[key[:2]].remove(key)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('probe_directory', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--only', nargs='+', choices=ORDER, help='Diagnostic subset; the integrate gate needs the full order')
    args = parser.parse_args()
    order = [name for name in ORDER if not args.only or name in args.only]
    probes = json.loads((args.probe_directory / 'Probes.json').read_text())
    blocked = json.loads((args.probe_directory / 'BlockedEdges.json').read_text())
    reachable = {t['name']: t['nearest_reachable']['location']
                 for t in json.loads((args.probe_directory / 'Report.json').read_text())['targets']}
    graph = FloorGraph(probes, blocked)
    avoid(graph, RUNTIME_AVOIDANCE)
    block_ledges(graph)
    route = json.loads(Path(__file__).with_name('route.json').read_text())
    route['runtime_avoidance'] = RUNTIME_AVOIDANCE
    home = current = route['segments'][-1]['points'][-1]
    for name, target in [(name, reachable[name]) for name in order] + [('region_return', home)]:
        path = graph.path(current, target)
        endpoint = path[-1]['location']
        assert math.dist(endpoint, target) < 200, (name, 'No floor near requested endpoint', endpoint)
        route['segments'].append({'name': name, 'points': [p['location'] for p in path],
                                  'floor_actors': sorted({p['actor'] for p in path if p['actor']})})
        current = endpoint
    args.output.write_text(json.dumps(route, indent=2) + '\n', encoding='utf-8')
    print([(s['name'], len(s['points'])) for s in route['segments']])


if __name__ == '__main__':
    main()
