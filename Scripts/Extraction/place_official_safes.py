"""把「原版游戏的保险箱位置」写进 DA_SpaceCenter_Ma1_Loot（在编辑器里跑）。

数据源：Artifacts/Extraction/RestorationEvidence/Loot/OfficialSafes.json
        由 Scripts/Extraction/build_official_safes.py 从两份官方数据合成：
          - 解包关卡里 SafeBox / SmallSafebox actor 的序列化根坐标（精度最高）
          - 官方 MapPOIData 里 bHighValueBox 的行（补 actor 导出漏掉的实体）
        世界坐标 = 源坐标 + (-664000, +451000, +22400)，三处样本交叉验过。

和 configure_spacecenter_v2.py 的分工：
  - 本脚本只管保险箱：把布局里的 LargeSafe / SmallSafe 点**整体替换**为官方 12 个位。
  - v2 只管出生点、撤离点、常规容器；它不再生成保险箱（SAFES 已清空）。

为什么 Z 不做「从 900 向下探地面」：
  PlacementAudit 实测 authored Z 与关卡地面的偏差 ≤ 8cm，原版高度本身是对的；
  而保险箱有 6 个在 14~26 米高的楼层上（离心机二楼 1387、总裁室 2604、中控二楼 1632、
  H3核心室 1899、总裁室外 2443），固定窗口下探会直接掉到一楼 —— 那正是「位置不对」的成因。
  这里直接用 authored Z。

运行（编辑器 Output Log，Cmd 模式）：
    py "D:/Users/SCSJ/BrokenEdge2.0/Scripts/Extraction/place_official_safes.py"
    想顺带做地面校验（会切关卡、并保存所有脏包，稍慢）：把下面 VERIFY 改成 True 再跑。

幂等：每次都以 OfficialSafes.json 为准整体重建保险箱点。
"""
import json
import sys
from pathlib import Path

import unreal

VERIFY = False          # True = 额外切关卡做 ±150cm 地面校验（只诊断，不改坐标）

LAYOUT = '/Game/Extraction/Loot/DA_SpaceCenter_Ma1_Loot'
LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
SAFE_KINDS = ('LargeSafe', 'SmallSafe')
MODELS = {
    'LargeSafe': '/Game/Extraction/Loot/Models/props_collect_box_546_3P/'
                 'props_collect_box_546_3P/SkeletalMeshes/props_collect_box_546_3P',
    'SmallSafe': '/Game/Extraction/Loot/Models/props_collect_SmallSafeBox_802_3P/'
                 'props_collect_SmallSafeBox_802_3P/SkeletalMeshes/props_collect_SmallSafeBox_802_3P',
}
RECIPES = {
    'LargeSafe': '/Game/Extraction/Loot/DA_ProjectLargeSafeRecipe',
    'SmallSafe': '/Game/Extraction/Loot/DA_ProjectSmallSafeRecipe',
}
LABELS = {'LargeSafe': '大保险柜', 'SmallSafe': '小保险箱'}
CHECK_UP, CHECK_DOWN, STAND_NORMAL_Z = 150.0, 150.0, 0.7


def make_transform(row):
    w = row['world']
    # 这个写法与 configure_loot_layout.py 一致（已验证可写入）：rotation 必须传 Rotator，
    # 传 Quat 会报 Cannot nativize 'Quat' as 'Rotator'。
    return unreal.Transform(
        location=unreal.Vector(w['X'], w['Y'], w['Z']),
        rotation=unreal.Rotator(pitch=0.0, yaw=float(row.get('yaw', 0.0)), roll=0.0),
        scale=unreal.Vector(1.0, 1.0, 1.0))


def probe_floor(world, location):
    """在 authored Z 上下 ±150cm 内找可站立面；只用于校验，绝不改坐标。"""
    try:
        hit = unreal.StingerAssetLibrary.trace_traversal_surface(
            world, location + unreal.Vector(0, 0, CHECK_UP), location - unreal.Vector(0, 0, CHECK_DOWN))
    except Exception as exc:                                  # 校验不是必需品
        return {'ok': None, 'error': str(exc)}
    if not hit or len(hit) != 2:
        return {'ok': False, 'reason': 'no hit in +-150cm'}
    impact, normal = hit
    return {'ok': bool(normal.z >= STAND_NORMAL_Z), 'floor_z': round(impact.z, 2),
            'delta_z': round(location.z - impact.z, 2), 'normal_z': round(normal.z, 3)}


def build_points(official):
    assets = {}
    for kind in SAFE_KINDS:
        mesh = unreal.load_asset(MODELS[kind])
        recipe = unreal.load_asset(RECIPES[kind])
        assert mesh, MODELS[kind]
        assert recipe, RECIPES[kind]
        assets[kind] = (mesh, recipe)

    points = []
    for row in official:
        mesh, recipe = assets[row['kind']]
        point = unreal.ExtractionLootPoint()
        point.set_editor_property('id', row['stable_id'])
        point.set_editor_property('weight', 1.0)
        point.set_editor_property('enabled', True)
        point.set_editor_property('kind', row['kind'])
        point.set_editor_property('label', '%s %s' % (LABELS[row['kind']], row['room']))
        point.set_editor_property('transform', make_transform(row))
        point.set_editor_property('appearance', mesh)
        point.set_editor_property('recipe', recipe)
        point.set_editor_property('reference', '原版 %s → %s' % (row['source'], row['actor']))
        points.append(point)
    return points


def verify(points_report):
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    original = editor.get_editor_world().get_path_name().split('.')[0]
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    try:
        assert levels.load_level(LEVEL), LEVEL
        unreal.StingerAssetLibrary.finish_validation_asset_compilation()
        world = editor.get_editor_world()
        for entry in points_report:
            x, y, z = entry['world']
            entry['floor_check'] = probe_floor(world, unreal.Vector(x, y, z))
    finally:
        levels.load_level(original)


def main():
    lib = unreal.EditorAssetLibrary
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True), '请先停止 PIE'

    data_path = (Path(unreal.Paths.project_dir())
                 / 'Artifacts/Extraction/RestorationEvidence/Loot/OfficialSafes.json')
    data = json.loads(data_path.read_text(encoding='utf-8'))
    official = [p for p in data['points'] if p['kind'] in SAFE_KINDS]
    assert official, '官方保险箱清单为空：%s' % data_path

    layout = unreal.load_asset(LAYOUT)
    assert layout, LAYOUT

    built = build_points(official)
    report = {
        'source': str(data_path),
        'layout': LAYOUT,
        'counts': {'total': len(official),
                   'LargeSafe': sum(p['kind'] == 'LargeSafe' for p in official),
                   'SmallSafe': sum(p['kind'] == 'SmallSafe' for p in official)},
        'points': [{'id': p['stable_id'], 'kind': p['kind'], 'room': p['room'],
                    'world': [round(p['world']['X'], 1), round(p['world']['Y'], 1),
                              round(p['world']['Z'], 1)],
                    'yaw': round(float(p.get('yaw', 0.0)), 2),
                    'source': p['source']} for p in official],
        'warnings': [],
    }

    if VERIFY or '--verify' in sys.argv:
        verify(report['points'])
        for entry in report['points']:
            check = entry.get('floor_check') or {}
            if check.get('ok') is False:
                report['warnings'].append(
                    '%s %s (Z=%.0f) 地面校验未通过：%s —— 坐标保持原版值，请游戏里目视确认'
                    % (entry['kind'], entry['room'], entry['world'][2],
                       check.get('reason') or 'normal_z=%.2f' % check.get('normal_z', 0)))

    # 只替换保险箱点；布局里其它容器（常规箱子等）原样保留。
    # FName 的 str() 形式随版本变化，用子串判断最稳。
    def is_safe(point):
        text = str(point.get_editor_property('kind'))
        return any(kind in text for kind in SAFE_KINDS)

    others = [p for p in layout.get_editor_property('points') if not is_safe(p)]
    report['kept_other_points'] = len(others)
    # 钥匙房容器由钥匙卡体系写入，本脚本只统计不动；为 0 说明编辑器在复用旧的内存版本，
    # 一旦保存就会把它们连同其它新点一起写没 —— 这种覆盖是静默的，所以必须喊出来。
    report['key_containers'] = sum(
        'Key' in str(p.get_editor_property('recipe')) for p in others)
    if report['key_containers'] == 0:
        report['warnings'].append(
            '表内 0 个钥匙房容器：若该版本本应含 6 个，说明编辑器用的是旧内存版本 —— '
            '先在内容浏览器 Reload 该资产，再重跑本脚本')
    layout.set_editor_property('variant', 'SpaceCenter_Ma1')
    layout.set_editor_property('points', others + built)
    layout.set_editor_property(
        'group_policy',
        '大小保险 = 官方原版 12 个位（解包 actor 根坐标 + 官方 POI 高价值箱），固定刷新、不随机；'
        'X/Y/Z 均为 authored 值，不做地面重采样。其它容器为项目常规物资点。')
    assert lib.save_loaded_asset(layout), LAYOUT

    out = Path(unreal.Paths.project_saved_dir()) / 'Extraction/OfficialSafes/Report.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    counts = report['counts']
    print('OFFICIAL_SAFES_PLACED layout=%s total=%d large=%d small=%d kept_other=%d key=%d warnings=%d' % (
        LAYOUT, counts['total'], counts['LargeSafe'], counts['SmallSafe'],
        len(others), report['key_containers'], len(report['warnings'])))
    for entry in report['points']:
        check = entry.get('floor_check')
        if check is None:
            mark, extra = '   ', ''
        elif check.get('ok'):
            mark, extra = 'ok ', 'delta=%+.1f' % check.get('delta_z', 0.0)
        else:
            mark, extra = 'BAD', check.get('reason') or 'normal_z=%.2f' % check.get('normal_z', 0)
        print('  %s %-10s %-14s X=%9.1f Y=%9.1f Z=%9.1f %s' % (
            mark, entry['kind'], entry['room'], entry['world'][0], entry['world'][1],
            entry['world'][2], extra))
    for warning in report['warnings']:
        print('  WARN', warning)
    print('  REPORT', out)


if __name__ == '__main__':
    main()
