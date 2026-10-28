"""离线规划 7F 新区域的物资箱候选点与守卫巡逻环线（不碰资产）。

输入：7F 地面探针 Probes.json / BlockedEdges.json（Scripts/SpaceCenter/phase7f_probe.json 跑出），
只从「起点可达」的连通区域选点，保证玩家和守卫都走得到。
输出：Docs/Extraction/RestorationEvidence/Phase7FGameplay/Plan.json，由 place_7f_loot_guards.py 写入资产。

POI 坐标来自解包 POI 表（Saved/SpaceCenter/Phase7F/Reference/POIList.tsv），换算
world = source + (-664000, +451000)。容器点位与配方是项目布置，不是官方刷新点。

用法：
    python Scripts/Extraction/plan_7f_gameplay.py Saved/SpaceCenter/Phase7F/EditorRuns/Phase7FRouteProbe01
"""
import argparse
import collections
import json
import math
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts/SpaceCenter'))
from plan_route import FloorGraph  # noqa: E402
from plan_phase7f_route import RUNTIME_AVOIDANCE, avoid, block_ledges  # noqa: E402

OUT = ROOT / 'Docs/Extraction/RestorationEvidence/Phase7FGameplay/Plan.json'
START = [-1500, -300, 362]
SEED = 20261028

# 7F 新补区域的 POI（工程坐标）。打印室/工业区 18 m 内没有可达地面（内部不连通），暂不布点。
POIS = [
    ('WestDorm', '宿舍楼', [-16643, -15792]),
    ('LivingArea', '生活区', [-16791, -16798]),
    ('WestGate', '西区大门', [-13584, -6539]),
    ('StaffPassage1', '一号员工通道', [-6742, -17042]),
    ('StaffPassage2', '二号员工通道', [16097, -17093]),
    ('LiftRoom', '吊装室', [24121, -12894]),
    ('Assembly', '组装室', [30805, -12996]),
    ('Canning', '罐装区', [31899, 1094]),
    ('TestRange', '水平试车间', [21480, 20002]),
]
# 每个 POI 两组、每组三个候选，运行时每组按权重选一个刷箱。
GROUPS_PER_POI = 2
CANDIDATES_PER_GROUP = 3
SEARCH_RADIUS = 1800
CONTAINER_SPACING = 500
RECIPES = ['Supply', 'Medical', 'Gear', 'Valuable']

# 巡逻环线：以区域中心所在楼层为准，在同层可达地面上朝四个方向各取一点成环；
# 相邻点之间必须有测得的地面连接，绕行过长就缩小半径重取。
ROUTES = [
    ('ExtractionPatrol_7F_WestDorm', [-16600, -15800]),
    ('ExtractionPatrol_7F_WestGate', [-13600, -6700]),
    ('ExtractionPatrol_7F_EastIndustry', [30800, -12900]),
    ('ExtractionPatrol_7F_TestRange', [21600, 19900]),
]
ROUTE_RADII = [2500, 1800, 1200, 800]
SAME_FLOOR_CM = 120
MAX_LEG_CM = 6000


def components(graph):
    comp = {}
    for key in graph.nodes:
        if key in comp:
            continue
        comp[key] = key
        queue = collections.deque([key])
        while queue:
            for other in graph.adjacent(queue.popleft()):
                if other not in comp:
                    comp[other] = key
                    queue.append(other)
    return comp


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('probe_directory', type=Path)
    args = parser.parse_args()
    probes = json.loads((args.probe_directory / 'Probes.json').read_text())
    blocked = json.loads((args.probe_directory / 'BlockedEdges.json').read_text())
    graph = FloorGraph(probes, blocked)
    avoid(graph, RUNTIME_AVOIDANCE)
    block_ledges(graph)
    comp = components(graph)
    home = comp[graph.near(START)]
    reachable = [k for k in graph.nodes if comp[k] == home]
    # 同一格上方 2.5 m 内还有地面/顶面采样，视为有遮挡的室内点；物资箱优先放室内。
    levels = collections.defaultdict(list)
    for p in probes:
        levels[(round(p['location'][0] / 100), round(p['location'][1] / 100))].append(p['location'][2])

    def covered(key):
        z = graph.nodes[key]['location'][2]
        return any(250 <= other - z <= 600 for other in levels[key[:2]])

    rng = random.Random(SEED)
    containers, recipe_index = [], 0
    for poi_id, label, center in POIS:
        near = [k for k in reachable if math.dist(graph.nodes[k]['location'][:2], center) < SEARCH_RADIUS
                and graph.nodes[k]['normal'][2] > .95]
        near.sort(key=lambda k: (not covered(k), math.dist(graph.nodes[k]['location'][:2], center)))
        picked = []
        for key in near:
            location = graph.nodes[key]['location']
            if all(math.dist(location[:2], other['location'][:2]) >= CONTAINER_SPACING for other in picked):
                picked.append({'location': location, 'covered': covered(key), 'floor_actor': graph.nodes[key]['actor']})
            if len(picked) >= GROUPS_PER_POI * CANDIDATES_PER_GROUP:
                break
        assert len(picked) >= GROUPS_PER_POI, (poi_id, 'too few reachable floors', len(picked))
        rng.shuffle(picked)
        for group in range(GROUPS_PER_POI):
            members = picked[group::GROUPS_PER_POI][:CANDIDATES_PER_GROUP]
            recipe = RECIPES[recipe_index % len(RECIPES)]
            recipe_index += 1
            for index, member in enumerate(members):
                containers.append({'id': '7F_%s_%s%d' % (poi_id, 'AB'[group], index + 1), 'group': '7F_%s_%s' % (poi_id, 'AB'[group]),
                                   'poi': label, 'recipe': recipe, **member})

    def leg(a, b):
        path = graph.path(a, b)
        return sum(math.dist(p['location'], q['location']) for p, q in zip(path, path[1:]))

    def loop(center, radius):
        hub = min(reachable, key=lambda k: math.dist(graph.nodes[k]['location'][:2], center))
        if math.dist(graph.nodes[hub]['location'][:2], center) > 1500:
            return None
        floor = graph.nodes[hub]['location'][2]
        band = [k for k in reachable if abs(graph.nodes[k]['location'][2] - floor) < SAME_FLOOR_CM
                and math.dist(graph.nodes[k]['location'][:2], center) < radius]
        points = []
        for direction in [(1, 0), (0, 1), (-1, 0), (0, -1)]:
            key = max(band, key=lambda k: (graph.nodes[k]['location'][0] - center[0]) * direction[0]
                      + (graph.nodes[k]['location'][1] - center[1]) * direction[1])
            points.append(graph.nodes[key]['location'])
        legs = [round(leg(a, b)) for a, b in zip(points, points[1:] + points[:1])]
        return points, legs

    routes, failed = [], []
    for name, center in ROUTES:
        # POI 中心可能落在被设施隔开的小平台上；依次尝试周边 15/30 m 的备选中心。
        centers = [center] + [[center[0] + dx * d, center[1] + dy * d] for d in (1500, 3000)
                              for dx, dy in [(1, 0), (0, 1), (-1, 0), (0, -1)]]
        best = None
        for candidate in centers:
            for radius in ROUTE_RADII:
                found = loop(candidate, radius)
                if found and (best is None or max(found[1]) < max(best[1])):
                    best = (*found, candidate, radius)
                if found and max(found[1]) < MAX_LEG_CM:
                    break
            if best and max(best[1]) < MAX_LEG_CM:
                break
        if best and max(best[1]) < MAX_LEG_CM:
            routes.append({'name': name, 'points': best[0], 'center': best[2], 'radius_cm': best[3],
                           'leg_lengths_cm': best[1]})
        else:
            failed.append((name, best and best[1]))
    assert not failed, ('patrol legs too long at every center and radius', failed)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({
        'source_probe': str(args.probe_directory.as_posix()),
        'seed': SEED,
        'boundary': 'Project placement on measured reachable floors; not official loot or guard data.',
        'containers': containers, 'routes': routes}, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
    print('PLAN containers=%d groups=%d routes=%d' % (len(containers), len({c['group'] for c in containers}), len(routes)))
    for poi_id, label, _ in POIS:
        rows = [c for c in containers if c['id'].startswith('7F_%s_' % poi_id)]
        print('  %-14s %d candidates, %d covered' % (label, len(rows), sum(c['covered'] for c in rows)))
    for route in routes:
        print('  %-34s legs %s' % (route['name'], route['leg_lengths_cm']))


if __name__ == '__main__':
    main()
