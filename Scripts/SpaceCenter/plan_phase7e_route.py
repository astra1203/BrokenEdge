"""Build the regional walking route from measured capsule-clear floors."""
import argparse
import json
import math
from pathlib import Path

from plan_route import FloorGraph


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('probe_directory', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    probes = json.loads((args.probe_directory / 'Probes.json').read_text())
    blocked = json.loads((args.probe_directory / 'BlockedEdges.json').read_text())
    graph = FloorGraph(probes, blocked)
    route = json.loads(Path(__file__).with_name('route.json').read_text())
    current = route['segments'][-1]['points'][-1]
    targets = [
        ('control_entry', [4800, -23000, 449]),
        ('control_upper', [4800, -26200, 1637]),
        ('control_return', current),
        ('launch_arrival', [-4700, 27800, 883]),
        ('launch_platform', [-8100, 34000, 1365]),
        ('region_return', current),
    ]
    for name, target in targets:
        endpoint = graph.nodes[graph.near(target)]['location']
        assert math.dist(endpoint, target) < 200, (name, 'No floor near requested endpoint', endpoint)
        path = graph.path(current, endpoint)
        route['segments'].append({'name': name, 'points': [p['location'] for p in path],
                                  'floor_actors': sorted({p['actor'] for p in path if p['actor']})})
        current = endpoint
    args.output.write_text(json.dumps(route, indent=2) + '\n', encoding='utf-8')
    print([(s['name'], len(s['points'])) for s in route['segments']])


if __name__ == '__main__':
    main()
