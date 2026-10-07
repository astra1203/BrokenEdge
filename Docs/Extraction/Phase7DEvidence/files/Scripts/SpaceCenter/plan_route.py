"""Plan on measured multi-level capsule-clear floor samples; runtime walking is the acceptance test."""
import argparse
from collections import defaultdict
import heapq
import json
import math
from pathlib import Path


class FloorGraph:
    def __init__(self, probes, blocked=()):
        self.blocked = {frozenset((tuple(e['from']), tuple(e['to']))) for e in blocked}
        self.nodes = {}
        self.cells = defaultdict(list)
        for p in probes:
            if not p['clear']:
                continue
            x, y, z = p['location']
            key = (round(x / 100), round(y / 100), round(z / 5))
            self.nodes[key] = p
        for key in self.nodes:
            self.cells[key[:2]].append(key)

    def near(self, location):
        return min(self.nodes, key=lambda n: math.dist(self.nodes[n]['location'], location))

    def adjacent(self, key):
        for dx, dy in [(1, 0), (-1, 0), (0, 1), (0, -1)]:
            for other in self.cells[(key[0] + dx, key[1] + dy)]:
                if abs(self.nodes[key]['location'][2] - self.nodes[other]['location'][2]) < 80:
                    if frozenset((key, other)) not in self.blocked:
                        yield other

    def path(self, start, end):
        start, end = self.near(start), self.near(end)
        queue, costs, parents = [(0, start)], {start: 0}, {start: None}
        while queue:
            _, node = heapq.heappop(queue)
            if node == end:
                result = []
                while node is not None:
                    result.append(self.nodes[node])
                    node = parents[node]
                return result[::-1]
            for other in self.adjacent(node):
                cost = costs[node] + math.dist(self.nodes[node]['location'], self.nodes[other]['location'])
                if cost < costs.get(other, math.inf):
                    parents[other], costs[other] = node, cost
                    heapq.heappush(queue, (cost + math.dist(self.nodes[other]['location'], self.nodes[end]['location']), other))
        raise ValueError('No measured floor connection between ' + str(start) + ' and ' + str(end))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('probes', type=Path)
    parser.add_argument('waypoints', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    edges = args.probes.with_name('BlockedEdges.json')
    graph = FloorGraph(json.loads(args.probes.read_text(encoding='utf-8')), json.loads(edges.read_text()) if edges.exists() else [])
    waypoints = json.loads(args.waypoints.read_text(encoding='utf-8'))
    segments = []
    for a, b in zip(waypoints, waypoints[1:]):
        path = graph.path(a['location'], b['location'])
        segments.append({'name': b['name'], 'points': [p['location'] for p in path], 'look_at': b.get('look_at'), 'floor_actors': sorted(set(p['actor'] for p in path))})
    parking = graph.path(waypoints[-1]['location'], [-1700, -300, 362])
    args.output.write_text(json.dumps({'origin_cm': [664000, -451000, -22400], 'segments': segments,
                                     'parking': [p['location'] for p in parking]}, indent=2) + '\n', encoding='utf-8')
    print([(s['name'], len(s['points'])) for s in segments])
