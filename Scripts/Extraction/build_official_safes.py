"""合并两份官方数据源，产出「原版保险箱位置」权威清单（不需要编辑器）。

源 A：Docs/Extraction/RestorationEvidence/Loot/SourcePoints.json
      —— 从解包关卡的 SafeBox / SmallSafebox actor 直接序列化导出，
         有 actor 根坐标 + Yaw + Scale，精度最高。
源 B：.workbuddy/tmp/df2/MapPOIData_SpaceCenter.json
      —— 官方 MapPOIData 里 HighValueBoxInfo.bHighValueBox == True 的行，
         即地图 UI 上标出来的高价值容器；Coord 是整数级精度。

坐标换算（三处独立样本验证一致，见 SourcePoints 的 source_position / world_position）：
    world.X = source.X - 664000
    world.Y = source.Y + 451000
    world.Z = source.Z + 22400

去重：X/Y 平面距离 < 100cm 视为同一实体（POI 是整数精度，实测同一 actor 差 41.65cm）。

输出：Docs/Extraction/RestorationEvidence/Loot/OfficialSafes.json
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_POINTS = ROOT / 'Docs/Extraction/RestorationEvidence/Loot/SourcePoints.json'
POI_TABLE = ROOT / '.workbuddy/tmp/df2/MapPOIData_SpaceCenter.json'
OUT = ROOT / 'Docs/Extraction/RestorationEvidence/Loot/OfficialSafes.json'

ORIGIN_X, ORIGIN_Y, ORIGIN_Z = 664000.0, 451000.0, 22400.0
VARIANTS = ('SpaceCenter_Ma1',)
DEDUP_CM = 100.0
UNDERGROUND_Z = -1000.0      # 原版里存在「被挪到地下的未使用 actor」，不进布局


def world(src):
    return {'X': src['X'] - ORIGIN_X, 'Y': src['Y'] + ORIGIN_Y, 'Z': src['Z'] + ORIGIN_Z}


def main():
    points = []
    for row in json.loads(SOURCE_POINTS.read_text(encoding='utf-8'))['points']:
        if row['variant'] not in VARIANTS:
            continue
        w = row['world_position']
        points.append({
            'id': row['id'],
            'kind': row['kind'],
            'label': '大保险柜' if row['kind'] == 'LargeSafe' else '小保险箱',
            'room': row['room'],
            'world': {'X': w['X'], 'Y': w['Y'], 'Z': w['Z']},
            'yaw': float(row.get('source_rotation', {}).get('Yaw', 0.0)),
            'actor': row['actor'],
            'source': 'SourcePoints/' + row['variant'],
            'evidence': 'serialized actor root',
        })

    poi_rows = json.loads(POI_TABLE.read_text(encoding='utf-8'))['Rows']
    added = []
    for row_name, row in poi_rows.items():
        if not (row.get('HighValueBoxInfo') or {}).get('bHighValueBox'):
            continue
        coord = row['Coord']
        w = world(coord)
        # actor 类型从行名判断：SmallSafebox -> 小保险箱
        kind = 'SmallSafe' if 'SmallSafebox' in row_name else 'LargeSafe'
        near = [p for p in points
                if (p['world']['X'] - w['X']) ** 2 + (p['world']['Y'] - w['Y']) ** 2 < DEDUP_CM ** 2]
        if near:
            for p in near:
                p.setdefault('poi_match', []).append({
                    'row': row_name,
                    'source_position': coord,
                    'delta_cm': round(((p['world']['X'] - w['X']) ** 2 + (p['world']['Y'] - w['Y']) ** 2) ** 0.5, 2),
                })
            continue
        entry = {
            'id': row_name.split('PersistentLevel_')[-1][:20],
            'kind': kind,
            'label': '大保险柜' if kind == 'LargeSafe' else '小保险箱',
            'room': row_name.split('航天城_')[-1].rsplit('_', 3)[0].replace('中心区_', ''),
            'world': w,
            'yaw': 0.0,
            'actor': row_name,
            'source': 'MapPOIData_SpaceCenter/HighValueBox',
            'evidence': 'official POI high-value box, integer coord; yaw unknown',
        }
        points.append(entry)
        added.append(entry)

    kept, dropped = [], []
    for p in points:
        (dropped if p['world']['Z'] < UNDERGROUND_Z else kept).append(p)

    # id 用 room+kind 命名，便于人读；保持稳定不随顺序变化
    seen = {}
    for p in kept:
        base = '%s_%s' % (p['kind'], p['room'])
        seen[base] = seen.get(base, 0) + 1
        p['stable_id'] = 'SRC_%s_%d' % (base, seen[base])

    kept.sort(key=lambda p: (p['kind'] != 'LargeSafe', p['room'], p['stable_id']))
    payload = {
        'note': 'Original game safe positions. world = source + (-664000, +451000, +22400). '
                'Z is authored, never re-traced.',
        'counts': {
            'LargeSafe': sum(p['kind'] == 'LargeSafe' for p in kept),
            'SmallSafe': sum(p['kind'] == 'SmallSafe' for p in kept),
        },
        'points': kept,
        'dropped': [{'id': p['id'], 'room': p['room'], 'kind': p['kind'],
                     'world': p['world'], 'reason': 'authored Z below playable floor (55m) in source map'}
                    for p in dropped],
        'poi_only_added': [p['stable_id'] for p in added if p['world']['Z'] >= UNDERGROUND_Z],
    }
    OUT.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('OFFICIAL_SAFES built=%d (large=%d small=%d) dropped=%d poi_only=%d' % (
        len(kept), payload['counts']['LargeSafe'], payload['counts']['SmallSafe'],
        len(dropped), len(payload['poi_only_added'])))
    for p in kept:
        print('  %-12s %-22s %-24s X=%9.1f Y=%9.1f Z=%9.1f yaw=%7.1f  [%s]' % (
            p['kind'], p['stable_id'][:22], p['room'][:22],
            p['world']['X'], p['world']['Y'], p['world']['Z'], p['yaw'], p['source']))
    for p in dropped:
        print('  DROPPED %-10s %-20s Z=%.1f' % (p['kind'], p['room'], p['world']['Z']))


if __name__ == '__main__':
    main()
