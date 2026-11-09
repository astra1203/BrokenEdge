"""把官方 45 个 PlayerStart（9 队）与 3 个官方撤离点写进地图定义。

数据来源：Artifacts/Extraction/RestorationEvidence/SpawnExit/OfficialSpawnExitAnchors.json
（由 build_official_spawn_exits.py 从解包 GameplayConfig 抽取，不是手填的坐标）

两个阶段：
  1. 碰撞复核 —— 官方 authored 变换只是"原版摆在哪"，不代表本工程已还原的地面能站。
     逐点探测：先按 authored Z 收窄窗口，再放宽高度，最后小半径 XY 外扩。
     外扩过的点会标成 Inferred 并在报告里写明偏移量，不会伪装成原版精确位。
  2. 写资产 —— 只改 SpawnGroups 与 Exits，物资布局、FloorBounds、Overview 一律不动。

在编辑器 Output Log（Cmd 模式）运行：
    py "D:/Users/SCSJ/BrokenEdge2.0/Scripts/Extraction/place_official_spawn_exits.py"
先只看覆盖情况、不写资产（推荐第一次这样跑）：
    py ".../place_official_spawn_exits.py" --audit
官方组落不下 2 队时，用现有配置兜底保证能开局：
    py ".../place_official_spawn_exits.py" --fallback-legacy
默认按官方延迟/关闭规则开放撤离点；调试时可退回开局全开、整局不关：
    py ".../place_official_spawn_exits.py" --open-at-start
外扩半径（默认 15 m）：
    py ".../place_official_spawn_exits.py" --snap-radius=3000
逐点落空时的区内重扫半径（默认 25 m，只补不足、不覆盖已命中点）：
    py ".../place_official_spawn_exits.py" --rescan-radius=0     # 关掉重扫
重扫允许的高度差（默认 600 cm，防止同组混进上下层；救高层点要放大）：
    py ".../place_official_spawn_exits.py" --rescan-radius=4000 --rescan-z=1200
成组门槛（默认每组至少 3 个可站立点）：
    py ".../place_official_spawn_exits.py" --min-group-points=2

幂等：每次整体重建这两张表，可反复运行。
"""
import json
import math
import sys
from pathlib import Path

import unreal

LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
ASSET = '/Game/Extraction/Maps/DA_SpaceCenterGameplay'
ANCHORS = 'Artifacts/Extraction/RestorationEvidence/SpawnExit/OfficialSpawnExitAnchors.json'

CAPSULE_RADIUS = 34.0      # 项目角色胶囊，见 Artifacts/Extraction/Traversal/SourceCorrection.md
CAPSULE_HALF = 92.0
GROUND_NORMAL_MIN = 0.7

# 撤离时序照官方 GameplayConfig 原值，不折算：试验场 FirstDelayTime 540s、
# 广场为电闸式：4 个 BP_ExitTriggerSquare 每局随机启用 2 个，全部拉下后开放 LimitedTime 120s
# （与 patch_square_switches.py 写入的数据一致）。单局时长不在解包数据里，按官方玩法 30 分钟，
# 与 AExtractionGameMode 默认 RoundSeconds 一致。--open-at-start 可退回开局全开（仅调试用）。
ROUND_SECONDS = 1800.0

# 按项目决定不启用的官方撤离点（不是探测失败）；报告 dropped_exits 会写明原因。
EXCLUDED_EXITS = {
    'OfficialExitDemoulin': '项目不启用：德穆兰撤离点（用户决定，2026-10-28）',
}
OPEN_TIME_SCALE = 1.0
HOLD_SECONDS = 10.0

# 探测顺序：越靠前越接近原版 authored 位置。
Z_WINDOWS = (300.0, 1200.0)
SNAP_RINGS = (300.0, 600.0, 900.0, 1200.0, 1500.0)
SNAP_DIRECTIONS = 8

# 逐点都落不下时的兜底：以官方组中心为圆心重扫地面。
# 只救"地形偏了十几米"的组（如 Assembly 两点在 10m 外命中），救不了"根本没这块地形"的组。
RESCAN_RADIUS = 2500.0
RESCAN_STEP = 150.0       # 网格步长；250/400 都会漏掉窄平台（Assembly 实测）
RESCAN_MIN_Z = 600.0      # 重扫只认大致同层的地面（组内 authored Z 相差不到 30 cm，600 足够）
RESCAN_SPACING = 150.0    # 重扫点最小间距（胶囊半径 34，150 cm 不会叠人）
GROUP_POINTS_WANT = 5     # 每组想要几个站位


def arg_number(name, default):
    prefix = '--%s=' % name
    for item in sys.argv:
        if item.startswith(prefix):
            return float(item[len(prefix):])
    return default


def status(name):
    return getattr(unreal.ExtractionReferenceStatus, name, unreal.ExtractionReferenceStatus.TEMPORARY)


# Unreal 的 Python 绑定对 bool 的 b 前缀处理不一致：
# FExtractionExitDefinition::bRequireEmptyPack 暴露成 require_empty_pack（b 被吃掉），
# 而 HalfExtent 就是 half_extent。所以不硬编码名字，按候选名试 + dir() 归一化兜底。
def _norm_prop(name):
    plain = name.lower().replace('_', '')
    return plain[1:] if len(plain) > 1 and plain[0] == 'b' and plain[1].isalpha() else plain


def _resolve_prop(obj, candidates):
    available = [n for n in dir(obj) if not n.startswith('_')]
    for name in candidates:
        if name in available:
            return name
    for simplify in (lambda n: n.lower().replace('_', ''), _norm_prop):
        targets = {simplify(c) for c in candidates}
        for name in available:
            if simplify(name) in targets:
                return name
    raise RuntimeError('%s 上找不到属性 %s；可用属性：%s' % (
        type(obj).__name__, ' / '.join(candidates), ', '.join(available)))


def set_prop(obj, value, *candidates):
    last = None
    for name in candidates:
        try:
            obj.set_editor_property(name, value)
            return name
        except Exception as exc:      # 只是名字不对，换下一个候选名重试
            last = exc
    try:
        name = _resolve_prop(obj, candidates)
    except RuntimeError as exc:
        raise RuntimeError('%s\n  最后一次尝试报错：%s' % (exc, last))
    obj.set_editor_property(name, value)
    return name


def get_prop(obj, *candidates):
    last = None
    for name in candidates:
        try:
            return obj.get_editor_property(name)
        except Exception as exc:
            last = exc
    try:
        name = _resolve_prop(obj, candidates)
    except RuntimeError as exc:
        raise RuntimeError('%s\n  最后一次尝试报错：%s' % (exc, last))
    return obj.get_editor_property(name)


EXIT_COPY_FIELDS = (
    ('id',),
    ('label',),
    ('transform',),
    ('half_extent',),
    ('bounds_offset',),
    ('hold_seconds',),
    ('opens_after_seconds',),
    ('closes_after_seconds',),
    ('capacity',),
    ('require_empty_pack', 'bRequireEmptyPack', 'b_require_empty_pack'),
)


def floor_at(world, x, y, z, window):
    """从 z+window 垂直下探，返回 (location, normal_z) 或 None。"""
    top = unreal.Vector(x, y, z + window)
    bottom = unreal.Vector(x, y, z - window)
    hit = unreal.StingerAssetLibrary.trace_traversal_surface(world, top, bottom)
    if not hit or len(hit) != 2:
        return None
    location, normal = hit
    if normal.z < GROUND_NORMAL_MIN:
        return None
    return location


def standable(world, location):
    center = location + unreal.Vector(0.0, 0.0, CAPSULE_HALF + 3.0)
    return unreal.StingerAssetLibrary.traversal_capsule_clear(
        world, center, center + unreal.Vector(0.0, 0.0, 0.01), CAPSULE_RADIUS, CAPSULE_HALF)


def dist2d(a, b):
    """水平距离。unreal.Vector 的 Python 绑定没有 Size2D()，只能自己算。"""
    return math.hypot(a.x - b.x, a.y - b.y)


def scan_area(world, center_xy, z_ref, radius, want, min_z=RESCAN_MIN_Z):
    """以官方组中心为圆心、由近到远网格扫地面，返回 [(location, dz), ...]。

    用途：个别官方点因为地形偏了十几米而落空时，仍在这片区域里找出能站的位。
    找到的点不是 PlayerStart 原位，调用方必须按 Inferred 记账。

    用笛卡尔网格而不是同心环：环采样在方形小平台上会整片漏采（环上的点恰好都在平台外）。
    """
    cx, cy = float(center_xy[0]), float(center_xy[1])
    steps = int(radius // RESCAN_STEP)
    # 网格对齐世界坐标而不是组中心：从中心起算会让格点相位随机，
    # 小平台可能整片落在两个格点之间（实测 Assembly 就这样漏掉）。
    base_x = RESCAN_STEP * round(cx / RESCAN_STEP)
    base_y = RESCAN_STEP * round(cy / RESCAN_STEP)
    grid = []
    for i in range(-steps, steps + 1):
        for j in range(-steps, steps + 1):
            x = base_x + i * RESCAN_STEP
            y = base_y + j * RESCAN_STEP
            grid.append((math.hypot(x - cx, y - cy), x, y))
    grid.sort()
    picked = []
    for distance, x, y in grid:
        if distance > radius:
            break
        location = floor_at(world, x, y, z_ref, min_z)
        if not location:
            continue
        dz = location.z - z_ref
        if abs(dz) > min_z:
            continue
        if not standable(world, location):
            continue
        if any(dist2d(location, other) < RESCAN_SPACING for other, _ in picked):
            continue
        picked.append((location, round(dz, 1)))
        if len(picked) >= want:
            break
    return picked


def resolve(world, xyz, snap_rings):
    """返回 dict(state, location, xy_offset, z_offset)。state: authored|z_shifted|xy_snapped|missing。"""
    x, y, z = float(xyz[0]), float(xyz[1]), float(xyz[2])
    # 大幅改高度的结果只作备选：原位正中被物件占住时，同层平移几米比跳到
    # 楼上/屋顶更接近原版（实测德穆兰撤离点会被抬到 8 m 高的顶面上）。
    shifted = None
    for window in Z_WINDOWS:
        location = floor_at(world, x, y, z, window)
        if not location:
            continue
        dz = abs(location.z - z)
        if dz > window:
            continue
        if not standable(world, location):
            continue
        if dz <= Z_WINDOWS[0]:
            return {'state': 'authored', 'location': location, 'xy_offset': 0.0, 'z_offset': round(location.z - z, 1)}
        shifted = {'state': 'z_shifted', 'location': location, 'xy_offset': 0.0, 'z_offset': round(location.z - z, 1)}
        break
    missing = shifted or {'state': 'missing', 'location': None, 'xy_offset': None, 'z_offset': None}
    if not snap_rings:
        return missing
    # XY 外扩：只在同高度带内找，避免把二楼点位吸到一楼地面。
    for ring in snap_rings:
        for step in range(SNAP_DIRECTIONS):
            angle = 2.0 * math.pi * step / SNAP_DIRECTIONS
            nx = x + math.cos(angle) * ring
            ny = y + math.sin(angle) * ring
            location = floor_at(world, nx, ny, z, Z_WINDOWS[0])
            if not location:
                continue
            if abs(location.z - z) > Z_WINDOWS[0]:
                continue
            if not standable(world, location):
                continue
            return {'state': 'xy_snapped', 'location': location, 'xy_offset': round(ring, 1),
                    'z_offset': round(location.z - z, 1)}
    return missing


def make_transform(location, yaw):
    return unreal.Transform(location=location,
                            rotation=unreal.Rotator(pitch=0.0, yaw=float(yaw), roll=0.0),
                            scale=unreal.Vector(1.0, 1.0, 1.0))


def main():
    audit_only = '--audit' in sys.argv
    official_timing = '--open-at-start' not in sys.argv
    fallback_legacy = '--fallback-legacy' in sys.argv
    snap_radius = arg_number('snap-radius', SNAP_RINGS[-1])
    snap_rings = tuple([r for r in SNAP_RINGS if r < snap_radius - 1.0] + [snap_radius])
    min_points = int(arg_number('min-group-points', 3))
    rescan_radius = arg_number('rescan-radius', RESCAN_RADIUS)
    rescan_z = arg_number('rescan-z', RESCAN_MIN_Z)
    project = Path(unreal.Paths.project_dir())
    payload = json.loads((project / ANCHORS).read_text(encoding='utf-8'))

    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True), '请先停止 PIE'
    original = editor.get_editor_world().get_path_name().split('.')[0]
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)

    report = {
        'source': ANCHORS,
        'source_sha256': payload.get('source_sha256'),
        'level': LEVEL,
        'audit_only': audit_only,
        'open_time_scale': OPEN_TIME_SCALE,
        'snap_rings': list(snap_rings),
        'min_group_points': min_points,
        'rescan_radius': rescan_radius,
        'rescan_z': rescan_z,
        'spawn_groups': [],
        'spawn_points': [],
        'exits': [],
        'dropped_groups': [],
        'dropped_exits': [],
        'legacy_fallback': [],
        'result': 'pending',
    }

    assert levels.load_level(LEVEL), LEVEL
    try:
        unreal.StingerAssetLibrary.finish_validation_asset_compilation()
        world = editor.get_editor_world()

        # --- 出生组 ---
        groups = []
        for group in payload['spawn_groups']:
            keep = []
            states = []
            for point in group['points']:
                found = resolve(world, point['world'], snap_rings)
                row = {'group': group['id'], 'actor': point['actor'],
                       'authored': point['world'], 'state': found['state'],
                       'xy_offset': found['xy_offset'], 'z_offset': found['z_offset']}
                if found['location'] is not None:
                    location = found['location']
                    row['resolved'] = [round(location.x, 1), round(location.y, 1), round(location.z, 1)]
                    keep.append((point, location, found))
                report['spawn_points'].append(row)
                states.append(found['state'])
                if found['state'] == 'missing':
                    print('  MISS SPAWN %-14s %-16s authored=%s' % (
                        group['id'], point['actor'], [round(v, 1) for v in point['world']]))
            # 逐点落空太多时，以官方组中心为圆心重扫这一带；只补不足，不覆盖已命中的原点。
            rescanned = 0
            if len(keep) < min_points and rescan_radius > 0:
                xs = [float(p['world'][0]) for p in group['points']]
                ys = [float(p['world'][1]) for p in group['points']]
                zs = sorted(float(p['world'][2]) for p in group['points'])
                z_ref = zs[len(zs) // 2]
                yaw_ref = group['points'][0]['yaw']
                for location, dz in scan_area(world, (sum(xs) / len(xs), sum(ys) / len(ys)),
                                              z_ref, rescan_radius, GROUP_POINTS_WANT, rescan_z):
                    if any(dist2d(location, kept) < RESCAN_SPACING for _, kept, _ in keep):
                        continue
                    keep.append(({'actor': 'Rescan%02d' % (len(keep) + 1), 'yaw': yaw_ref,
                                  'world': [round(location.x, 1), round(location.y, 1),
                                            round(location.z, 1)]},
                                 location, {'state': 'rescanned', 'z_offset': dz}))
                    rescanned += 1
                    if len(keep) >= GROUP_POINTS_WANT:
                        break
                if rescanned:
                    states.extend(['rescanned'] * rescanned)
                    print('  RESCAN GROUP %-18s +%d 点（中心 %.0f,%.0f 参考Z=%.0f）'
                          % (group['id'], rescanned, sum(xs) / len(xs), sum(ys) / len(ys), z_ref))
            if len(keep) < min_points:
                report['dropped_groups'].append({
                    'group': group['id'], 'kept': len(keep), 'states': states,
                    'reason': '可站立点不足 %d 个' % min_points})
                print('  DROP GROUP %-20s kept=%d/%d' % (group['id'], len(keep), len(group['points'])))
                continue
            positions = [make_transform(location, point['yaw']) for point, location, _ in keep]
            exact = all(found['state'] == 'authored' for _, _, found in keep)
            definition = unreal.ExtractionSpawnGroup()
            try:
                set_prop(definition, group['id'], 'id')
                set_prop(definition, positions, 'positions')
                set_prop(definition, status('VERIFIED' if exact else 'INFERRED'), 'reference_status')
                set_prop(definition, '官方 GameplayConfig %s（%s 其中 %d 点）%s' % (
                    group['id'], ' / '.join(p['actor'] for p, _, _ in keep), len(keep),
                    '；碰撞精确命中 authored 位' if exact else
                    '；部分点做了 XY/Z 纠偏%s，见 Report.json' % (
                        '，其中 %d 点为区内重扫' % rescanned if rescanned else '')),
                    'reference')
            except Exception as exc:
                report['dropped_groups'].append({'group': group['id'], 'kept': len(keep),
                                                 'states': states, 'reason': '构造失败：%s' % exc})
                print('  FAIL GROUP %-20s %s' % (group['id'], exc))
                continue
            groups.append(definition)
            report['spawn_groups'].append({
                'id': group['id'], 'kept': len(keep), 'authored': len(group['points']),
                'exact': exact, 'states': states,
                'actors': [p['actor'] for p, _, _ in keep]})

        # --- 撤离点 ---
        exits = []
        for row in payload['exits']:
            if row['id'] in EXCLUDED_EXITS:
                report['dropped_exits'].append({'id': row['id'], 'authored': row['world'],
                                                'reason': EXCLUDED_EXITS[row['id']]})
                print('  SKIP EXIT  %-22s %s' % (row['id'], EXCLUDED_EXITS[row['id']]))
                continue
            found = resolve(world, row['world'], snap_rings)
            if found['location'] is None:
                report['dropped_exits'].append({'id': row['id'], 'authored': row['world'],
                                                'reason': '该位置没有可站立地面'})
                print('  MISS EXIT  %-22s authored=%s' % (row['id'], row['world']))
                continue
            # 默认照官方原值（OPEN_TIME_SCALE=1）；--open-at-start 时一律 0（开局即开、整局不关）。
            switch = row['open_type'] == 'Switch' and official_timing
            if switch:
                sys.path.insert(0, str(project / 'Scripts/Extraction'))
                import patch_square_switches
                switch_locations, switches_required, switch_window = patch_square_switches.switch_settings(payload)
                opens, closes = 0.0, 0.0
            elif official_timing:
                opens = round(row['open_seconds'] * OPEN_TIME_SCALE, 1)
                closes = (round(opens + row['close_seconds_after_open'] * OPEN_TIME_SCALE, 1)
                          if row['close_seconds_after_open'] else 0.0)
            else:
                opens, closes = 0.0, 0.0
            assert opens + HOLD_SECONDS < ROUND_SECONDS, (row['id'], opens)
            if closes:
                assert closes < ROUND_SECONDS, (row['id'], closes)
            definition = unreal.ExtractionExitDefinition()
            try:
                set_prop(definition, row['id'], 'id')
                set_prop(definition, row['label'], 'label')
                set_prop(definition, make_transform(found['location'], row['yaw']), 'transform')
                set_prop(definition, unreal.Vector(250.0, 250.0, 132.5), 'half_extent')
                set_prop(definition, unreal.Vector(0.0, 0.0, 117.5), 'bounds_offset')
                set_prop(definition, HOLD_SECONDS, 'hold_seconds')
                set_prop(definition, float(opens), 'opens_after_seconds')
                set_prop(definition, float(closes), 'closes_after_seconds')
                set_prop(definition, int(row['capacity']), 'capacity')
                set_prop(definition, bool(row['require_empty_pack']),
                         'require_empty_pack', 'bRequireEmptyPack', 'b_require_empty_pack')
                if switch:
                    set_prop(definition, switch_locations, 'switch_locations')
                    set_prop(definition, switches_required, 'switches_required')
                    set_prop(definition, switch_window, 'switch_open_seconds')
                set_prop(definition, status('VERIFIED' if found['state'] == 'authored' else 'INFERRED'),
                         'reference_status')
                set_prop(definition, '官方 %s / PlayerExitId=%s。%s' % (
                    row['actor'], row['player_exit_id'], row['note']), 'reference')
            except Exception as exc:
                # 单个撤离点写不成不该拖死整次运行：记进报告，继续下一个。
                report['dropped_exits'].append({'id': row['id'], 'authored': row['world'],
                                                'reason': '构造失败：%s' % exc})
                print('  FAIL EXIT  %-22s %s' % (row['id'], exc))
                continue
            exits.append(definition)
            location = found['location']
            report['exits'].append({
                'id': row['id'], 'actor': row['actor'], 'label': row['label'],
                'player_exit_id': row['player_exit_id'],
                'authored': row['world'], 'state': found['state'],
                'xy_offset': found['xy_offset'], 'z_offset': found['z_offset'],
                'world': [round(location.x, 1), round(location.y, 1), round(location.z, 1)],
                'opens': opens, 'closes': closes, 'capacity': row['capacity'],
                'require_empty_pack': row['require_empty_pack'],
                'official_open_seconds': row['open_seconds'],
                'official_close_after_open': row['close_seconds_after_open']})

        report['spawn_group_count'] = len(groups)
        report['exit_count'] = len(exits)
        states = [p['state'] for p in report['spawn_points']]
        report['spawn_point_states'] = {s: states.count(s) for s in sorted(set(states))}

        out = Path(unreal.Paths.project_saved_dir()) / 'Extraction/OfficialSpawnExit/Report.json'
        out.parent.mkdir(parents=True, exist_ok=True)

        def flush():
            out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

        flush()

        print('OFFICIAL_SPAWN_EXIT_AUDIT groups=%d/%d exits=%d/%d spawn_points_kept=%d/%d' % (
            len(groups), len(payload['spawn_groups']), len(exits), len(payload['exits']),
            sum(g['kept'] for g in report['spawn_groups']), payload['spawn_count']))
        print('  spawn point states: %s' % report['spawn_point_states'])
        for row in report['spawn_groups']:
            print('  GROUP %-20s kept=%d/%d exact=%s' % (row['id'], row['kept'], row['authored'], row['exact']))
        for row in report['exits']:
            print('  EXIT %-22s %-8s state=%-10s world=%s open=%.0fs close=%.0fs cap=%d empty=%s' % (
                row['id'], row['label'], row['state'], row['world'], row['opens'], row['closes'],
                row['capacity'], row['require_empty_pack']))

        if audit_only:
            print('AUDIT_ONLY 资产未改动；去掉 --audit 即写入')
            print('  报告：%s' % out)
            return

        # 运行时要求 SpawnGroups>=2 且至少有一个撤离点，否则整局无法开始。
        if len(groups) < 2 or not exits:
            if fallback_legacy:
                asset_old = unreal.load_asset(ASSET)
                old_groups = list(asset_old.get_editor_property('spawn_groups')) if asset_old else []
                old_exits = list(asset_old.get_editor_property('exits')) if asset_old else []
                for old in old_groups:
                    if len(groups) >= 2:
                        break
                    clone = unreal.ExtractionSpawnGroup()
                    set_prop(clone, 'Legacy_%s' % old.get_editor_property('id'), 'id')
                    set_prop(clone, old.get_editor_property('positions'), 'positions')
                    set_prop(clone, status('TEMPORARY'), 'reference_status')
                    set_prop(clone, '兜底：旧配置组，非官方点位。官方组落不下时临时保留', 'reference')
                    groups.append(clone)
                    report['legacy_fallback'].append('group:Legacy_%s' % old.get_editor_property('id'))
                for old in old_exits:
                    if exits:
                        break
                    clone = unreal.ExtractionExitDefinition()
                    for field in EXIT_COPY_FIELDS:
                        set_prop(clone, get_prop(old, *field), *field)
                    set_prop(clone, 'Legacy_%s' % old.get_editor_property('id'), 'id')
                    set_prop(clone, status('TEMPORARY'), 'reference_status')
                    set_prop(clone, '兜底：旧撤离点，非官方点位', 'reference')
                    exits.append(clone)
                    report['legacy_fallback'].append('exit:Legacy_%s' % old.get_editor_property('id'))
                print('FALLBACK_LEGACY 追加旧配置 %d 项' % len(report['legacy_fallback']))
            if len(groups) < 2 or not exits:
                report['result'] = 'aborted'
                flush()
                print('OFFICIAL_SPAWN_EXIT_ABORTED 可站立组=%d 撤离点=%d（运行时门槛 2/1），资产未改动'
                      % (len(groups), len(exits)))
                print('  这张还原图覆盖不到官方点位；先补地形，或用 --fallback-legacy 保住能开局')
                print('  报告：%s' % out)
                return

        asset = unreal.load_asset(ASSET)
        assert asset, ASSET
        asset.set_editor_property('spawn_groups', groups)
        asset.set_editor_property('exits', exits)
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset), ASSET
        report['result'] = 'written'
        report['spawn_group_count'] = len(groups)
        flush()
        print('OFFICIAL_SPAWN_EXIT_WRITTEN groups=%d exits=%d' % (len(groups), len(exits)))
        print('  报告：%s' % out)
    finally:
        levels.load_level(original)


if __name__ == '__main__':
    main()
