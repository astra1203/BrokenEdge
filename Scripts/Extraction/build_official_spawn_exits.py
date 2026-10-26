"""把官方 GameplayConfig 里的 PlayerStart / PlayerExit 抽成干净的权威清单。

来源：Docs/Extraction/SpaceCenterExpansion/SourceGameplayAnchors.json
      （由 inspect_source_gameplay.py + summarize_source_gameplay.py 从解包包解码生成，
       记录了解码文件 SHA-256、原包路径、导出索引、原坐标与换算后坐标）

坐标换算（与物资、保险箱同一套约定，勿再猜）：
    world = source + (-664000, +451000, +22400)

产物：Docs/Extraction/RestorationEvidence/SpawnExit/OfficialSpawnExitAnchors.json
      只读、可重复生成；写资产的脚本读它，不直接解析解包数据。

用法：
    python Scripts/Extraction/build_official_spawn_exits.py
"""
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Docs/Extraction/SpaceCenterExpansion/SourceGameplayAnchors.json'
OUT_DIR = ROOT / 'Docs/Extraction/RestorationEvidence/SpawnExit'
ORIGIN = (-664000.0, 451000.0, 22400.0)

# 官方出生点按 PlayerStart 编号分带，每带 5 个 = 一队。
# 这是从对象名直接读出的分组，不是按距离猜的；下面会用距离做交叉校验。
SPAWN_BANDS = [
    ('OfficialWestGate', ['PlayerStart2', 'PlayerStart3', 'PlayerStart4', 'PlayerStart5', 'PlayerStart_1']),
    ('OfficialWestDorm', ['PlayerStart6_8', 'PlayerStart7', 'PlayerStart8', 'PlayerStart9', 'PlayerStart10']),
    ('OfficialControlNorth', ['PlayerStart21', 'PlayerStart22', 'PlayerStart23', 'PlayerStart24', 'PlayerStart25']),
    ('OfficialAssembly', ['PlayerStart31', 'PlayerStart32', 'PlayerStart33', 'PlayerStart34', 'PlayerStart35']),
    ('OfficialSoutheast', ['PlayerStart36', 'PlayerStart37', 'PlayerStart38', 'PlayerStart39', 'PlayerStart40']),
    ('OfficialEastGate', ['PlayerStart41', 'PlayerStart42', 'PlayerStart43', 'PlayerStart44', 'PlayerStart45']),
    ('OfficialNortheast', ['PlayerStart46', 'PlayerStart47', 'PlayerStart48', 'PlayerStart49', 'PlayerStart50']),
    ('OfficialLaunchSouth', ['PlayerStart51', 'PlayerStart52', 'PlayerStart53', 'PlayerStart54', 'PlayerStart55']),
    ('OfficialNorthRidge', ['PlayerStart56', 'PlayerStart57', 'PlayerStart58', 'PlayerStart59', 'PlayerStart60']),
]

# 官方 PlayerExit 的实例属性，逐条来自 SourceGameplayAnchors.json 的 authored_overrides / authored_rules。
# label 用中文是因为游戏里显示的是中文；英文名放在 actor 字段里保留可追溯性。
EXIT_META = {
    'BP_PlayerExit_Square': {
        'id': 'OfficialExitSquare',
        'label': '广场撤离点',
        'open_type': 'Switch',
        'open_seconds': 900.0,      # SwitchRandomInfos.ExpectedOpenTime
        'close_seconds_after_open': 120.0,   # ExitCloseConfig.AfterCountDown / LimitedTime
        'capacity': 0,
        'require_empty_pack': False,
        'note': '官方为开关式开放：4 个 BP_ExitTriggerDam 里随机 2 个，全部激活才开；本项目无开关逻辑，按 ExpectedOpenTime 折算为定时开放',
    },
    'BP_PlayerExit_Demoulin_3': {
        'id': 'OfficialExitDemoulin',
        'label': '德穆兰撤离点',
        'open_type': 'Delay',
        'open_seconds': 900.0,      # ExitOpenConfig.FirstDelayTime
        'close_seconds_after_open': 0.0,     # AfterCountDown，无 LimitedTime
        'capacity': 3,              # EnableMaxEscapedNum / MaxEscapedNum
        'require_empty_pack': False,
        'note': '官方另有 ExitConditionType=PayItem（5 个 151104000xx 或通行物 15200000303）与阵营限制；本项目暂无支付交互，未启用',
    },
    'BP_PlayerExit_Testrange_3': {
        'id': 'OfficialExitTestrange',
        'label': '试验场撤离点',
        'open_type': 'Delay',
        'open_seconds': 540.0,      # ExitOpenConfig.FirstDelayTime
        'close_seconds_after_open': 0.0,
        'capacity': 3,              # ExitCloseConfig.AfterEscapeCount / EscapeCount (Level1)
        'require_empty_pack': True,  # ExitConditionEquipment: Bag / Empty
        'note': '官方条件为背包清空（Bag=Empty）后离开，即丢包撤；容量 Level1=3、Level2=1，这里取 3',
    },
}


def world_of(source_position):
    return [round(float(source_position[i]) + ORIGIN[i], 2) for i in range(3)]


def dist2d(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def main():
    payload = json.loads(SOURCE.read_text(encoding='utf-8'))
    variants = {v['package'].split('/')[3]: v for v in payload['variants']}
    # Level1 与 Level2 的 45 个 PlayerStart 完全同址（只有 Yaw 有差异），
    # 用 Level1 为主，另做一致性断言，避免以后换源时静默漂移。
    primary = variants['SpaceCenter_Level1']
    reference = variants['SpaceCenter_Level2']

    def index_spawns(variant):
        found = {}
        for row in variant['spawns']:
            found[row['actor']] = row
        return found

    spawns = index_spawns(primary)
    spawns2 = index_spawns(reference)
    assert len(spawns) == 45, len(spawns)
    assert set(spawns) == set(spawns2), set(spawns) ^ set(spawns2)
    drifts = {k: dist2d(world_of(spawns[k]['source_position']), world_of(spawns2[k]['source_position']))
              for k in spawns}
    drift = max(drifts.values())
    worst = sorted(drifts.items(), key=lambda kv: -kv[1])[:5]
    # 两个变体共用同一套出生点，个别点有几十~几百 cm 的手工挪动属于正常；
    # 超过 1 m 的要能在报告里看见，不能静默当成"完全一致"。
    assert drift < 1500, worst

    groups = []
    for group_id, actors in SPAWN_BANDS:
        missing = [a for a in actors if a not in spawns]
        assert not missing, (group_id, missing)
        points = []
        for actor in actors:
            row = spawns[actor]
            world = world_of(row['source_position'])
            yaw = float((row.get('rotation') or {}).get('Yaw', 0.0))
            points.append({'actor': actor, 'world': world, 'yaw': yaw,
                           'export_index': row['export_index']})
        intra = max(dist2d(a['world'], b['world']) for a in points for b in points)
        assert intra < 2500, (group_id, intra)
        groups.append({'id': group_id, 'points': points, 'intra_span_cm': round(intra, 1)})

    # 组间距离：任意两组任意两点最近距离，确认 9 组确实是 9 个分开的出生区。
    separations = []
    for i in range(len(groups)):
        for j in range(i + 1, len(groups)):
            near = min(dist2d(a['world'], b['world'])
                       for a in groups[i]['points'] for b in groups[j]['points'])
            separations.append((round(near, 1), groups[i]['id'], groups[j]['id']))
    separations.sort()
    assert separations[0][0] > 2500, separations[0]

    exits = []
    for row in primary['exits']:
        meta = EXIT_META[row['actor']]
        world = world_of(row['source_position'])
        yaw = float((row.get('rotation') or {}).get('Yaw', 0.0))
        exits.append({
            'id': meta['id'],
            'actor': row['actor'],
            'label': meta['label'],
            'player_exit_id': (row.get('authored_overrides') or {}).get('PlayerExitId'),
            'world': world,
            'yaw': yaw,
            'open_type': meta['open_type'],
            'open_seconds': meta['open_seconds'],
            'close_seconds_after_open': meta['close_seconds_after_open'],
            'capacity': meta['capacity'],
            'require_empty_pack': meta['require_empty_pack'],
            'note': meta['note'],
            'export_index': row['export_index'],
            'authored_overrides': row.get('authored_overrides') or {},
        })
    assert len(exits) == 3, len(exits)

    triggers = []
    for row in primary['exit_triggers']:
        triggers.append({
            'actor': row['actor'],
            'world': world_of(list(row['source_position'].values())),
            'target': row['target']['ObjectName'].split("'")[0],
            'export_index': row['export_index'],
        })

    result = {
        'schema': 1,
        'source': str(SOURCE.relative_to(ROOT)).replace('\\', '/'),
        'source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        'decoded_sha256': payload.get('decoded_sha256'),
        'origin': list(ORIGIN),
        'conversion': 'world = source + (%d, %d, %d)' % ORIGIN,
        'variant_used': primary['package'],
        'variant_cross_check': {
            'against': reference['package'],
            'max_spawn_xy_drift_cm': round(drift, 2),
            'worst_spawns': [{'actor': k, 'drift_cm': round(v, 1)} for k, v in worst],
            'exits_identical': all(
                world_of(a['source_position']) == world_of(b['source_position'])
                for a, b in zip(primary['exits'], reference['exits'])),
        },
        'spawn_groups': groups,
        'spawn_count': sum(len(g['points']) for g in groups),
        'min_group_separation_cm': separations[0][0],
        'exits': exits,
        'exit_triggers': triggers,
        'caveat': '源自解包 GameplayConfig 的 authored 变换，未经运行时地面/通路验收；'
                  '写资产前必须用碰撞探测复核。',
    }

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out = OUT_DIR / 'OfficialSpawnExitAnchors.json'
    out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    print('OFFICIAL_ANCHORS_WRITTEN groups=%d spawns=%d exits=%d triggers=%d'
          % (len(groups), result['spawn_count'], len(exits), len(triggers)))
    print('  variant cross-check drift = %.2f cm, exits identical = %s'
          % (drift, result['variant_cross_check']['exits_identical']))
    print('  min group separation = %.0f cm' % separations[0][0])
    for g in groups:
        xs = [p['world'][0] for p in g['points']]
        ys = [p['world'][1] for p in g['points']]
        print('  %-22s span=%5.0fcm  center=(%8.0f,%8.0f)' % (
            g['id'], g['intra_span_cm'], (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2))
    for e in exits:
        print('  EXIT %-22s %-8s world=(%8.0f,%8.0f,%7.0f) yaw=%6.1f open=%5.0fs cap=%d empty_pack=%s'
              % (e['id'], e['player_exit_id'], e['world'][0], e['world'][1], e['world'][2],
                 e['yaw'], e['open_seconds'], e['capacity'], e['require_empty_pack']))


if __name__ == '__main__':
    main()
