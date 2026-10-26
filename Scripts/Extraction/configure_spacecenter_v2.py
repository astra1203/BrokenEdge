"""重建航天基地的出生点、撤离点与物资布局。

坐标来源分三类，脚本里逐条标注：
  [POI]  官方 MapPOIData_SpaceCenter（DataTables 解包），X/Y 取 original cm，
         世界坐标 = 源坐标 + (-664000, +451000)。这是本工程既有换算约定，
         可在 Scripts/Extraction/prepare_loot_sources.py:33 复核。
  [TRACE] 高度（Z）不采用 POI 的 Z 字段（那是地图标注高度，与关卡地面不共面），
         一律用引擎碰撞向下探测，探测不到的点直接丢弃。
  [PROJECT] 容器类型与内部配方是项目设定，不是官方掉率。

在编辑器 Output Log（Cmd 模式）运行：
    py "D:/Users/SCSJ/BrokenEdge2.0/Scripts/Extraction/configure_spacecenter_v2.py"

幂等：每次都以本文件的坐标表为准整体重建，可反复运行。
"""
import json
import math
from pathlib import Path
import unreal

LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
ASSET = '/Game/Extraction/Maps/DA_SpaceCenterGameplay'
ORIGIN_X, ORIGIN_Y = 664000.0, -451000.0

# 出生组与撤离点**不在本脚本生成**：
#   原版 45 个 PlayerStart / 3 个 PlayerExit 由 place_official_spawn_exits.py 写，
#   保险箱由 place_official_safes.py 写。本脚本只管物资容器，
#   以前那几组临时 POI 出生点/撤离点已移除，重跑也不会把它们写回去。
SAFES = []  # 保险箱不在这里生成：官方 12 个位由 place_official_safes.py 负责写，
            # 两处都写会在同一位置叠出重复容器。这里只补常规物资容器。
# ---- [PROJECT] 常规物资容器：撒在这些官方房间周围，类型为项目设定 ----
LOOT_ROOMS = [
    ('浮力室', (663050, -458573)),
    ('离心机室', (663082, -450868)),
    ('总裁室', (668840, -454438)),
    ('黑室', (674966, -451460)),
    ('蓝室', (674970, -459185)),
    ('核心区', (663348, -452675)),
    ('组装室', (694805, -463996)),
    ('打印室', (694966, -470283)),
    ('吊装室', (688121, -463894)),
    ('水平试车间', (685480, -430998)),
    ('罐装区', (695899, -449906)),
    ('中控楼', (668850, -476538)),
]
# ---- [POI] 战术地图区域框用的兴趣点 ----
AREA_POIS = [
    ('宿舍楼', (647357, -466792)), ('一号员工通道', (657258, -468042)),
    ('中控楼', (668850, -476538)), ('中控桥', (668859, -466598)),
    ('浮力室', (663050, -458573)), ('离心机室', (663082, -450868)),
    ('总裁室', (668840, -454438)), ('黑室', (674966, -451460)),
    ('蓝室', (674970, -459185)), ('东区吊桥', (684337, -448551)),
    ('水平试车间', (685480, -430998)), ('发射区', (658980, -423450)),
    ('吊装室', (688121, -463894)), ('组装室', (694805, -463996)),
    ('打印室', (694966, -470283)), ('西区大门', (650416, -457539)),
    ('二号员工通道', (680097, -468093)), ('罐装区', (695899, -449906)),
    ('核心区', (663348, -452675)), ('工业区', (694443, -471010)),
]
LOOT_KINDS = {
    'SupplyBox': [('Ammo', 30), ('Bandage', 2), ('Splint', 1)],
    'ToolBox': [('Tools', 2), ('Electronics', 1)],
    'MedBox': [('Medkit', 1), ('Bandage', 3)],
    'AmmoBox': [('AmmoAP', 20), ('Ammo', 40)],
}
SAFE_RECIPES = {'LargeSafe': [('Electronics', 3), ('Tools', 1)],
                'SmallSafe': [('Electronics', 1), ('Tools', 1)]}
ROOM_LOOT = ['SupplyBox', 'SupplyBox', 'ToolBox', 'MedBox', 'AmmoBox']  # 每个房间撒 5 个


def world_of(src):
    return float(src[0]) - ORIGIN_X, float(src[1]) - ORIGIN_Y


def ground(world, x, y, ceiling=900.0, floor=-600.0):
    """从 ceiling 向下探测可站立地面；返回 (location, ok)。"""
    top = unreal.Vector(x, y, ceiling)
    bottom = unreal.Vector(x, y, floor)
    hit = unreal.StingerAssetLibrary.trace_traversal_surface(world, top, bottom)
    if not hit or len(hit) != 2:
        return None
    location, normal = hit
    if normal.z < 0.7:          # 打到斜面/侧墙
        return None
    center = location + unreal.Vector(0, 0, 95)
    if not unreal.StingerAssetLibrary.traversal_capsule_clear(
            world, center, center + unreal.Vector(0, 0, 0.01), 34, 92):
        return None
    return location


def build_recipes(tools, lib):
    made = {}
    for name, stacks in list(LOOT_KINDS.items()) + list(SAFE_RECIPES.items()):
        asset_name = 'DA_Project' + name + 'Recipe'
        path = '/Game/Extraction/Loot/' + asset_name
        recipe = unreal.load_asset(path) if lib.does_asset_exist(path) else None
        if not recipe:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property('data_asset_class', unreal.ExtractionLootRecipe)
            recipe = tools.create_asset(asset_name, '/Game/Extraction/Loot',
                                        unreal.ExtractionLootRecipe, factory)
        items = []
        for item, quantity in stacks:
            stack = unreal.ExtractionLootStack()
            stack.set_editor_property('item', item)
            stack.set_editor_property('quantity', quantity)
            items.append(stack)
        recipe.set_editor_property('items', items)
        assert lib.save_loaded_asset(recipe), asset_name
        made[name] = recipe
    return made


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    lib = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True), '请先停止 PIE'
    original = editor.get_editor_world().get_path_name().split('.')[0]
    # 本脚本要切到目标关卡做碰撞探测；先把脏包落盘，避免切换时丢掉未保存的改动。
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    report = {'source': 'MapPOIData_SpaceCenter (DataTables) + engine collision',
              'origin': [ORIGIN_X, ORIGIN_Y], 'loot': [], 'dropped': []}

    assert levels.load_level(LEVEL), LEVEL
    try:
        unreal.StingerAssetLibrary.finish_validation_asset_compilation()
        world = editor.get_editor_world()

        # ---- 出生组与撤离点由 place_official_spawn_exits.py 负责，这里不再生成 ----

        # ---- 物资 ----
        recipes = build_recipes(tools, lib)
        points = []
        index = 0
        for room, src in LOOT_ROOMS:
            bx, by = world_of(src)
            for slot, kind in enumerate(ROOM_LOOT):
                angle = slot * 1.2566  # 72°
                x = bx + math.cos(angle) * 260
                y = by + math.sin(angle) * 260
                spot = ground(world, x, y)
                if not spot:
                    report['dropped'].append({'kind': 'loot', 'room': room,
                                              'at': [round(x, 1), round(y, 1)]})
                    continue
                points.append((room, kind, spot))
        loot_points = []
        for label, kind, spot in points:
            index += 1
            point = unreal.ExtractionLootPoint()
            point.set_editor_property('id', 'V2_%s_%03d' % (kind, index))
            point.set_editor_property('label', '%s %s' % (label, kind))
            point.set_editor_property('kind', kind)
            point.set_editor_property('weight', 1.0)
            point.set_editor_property('enabled', True)
            point.set_editor_property('transform', unreal.Transform(
                location=spot, rotation=unreal.Rotator(pitch=0, yaw=0, roll=0),
                scale=unreal.Vector(1, 1, 1)))
            point.set_editor_property('recipe', recipes[kind])
            point.set_editor_property('reference', 'POI %s + collision ground; kind is project tuning' % label)
            loot_points.append(point)
        assert loot_points, '一个物资点都没落地，放弃写入'

        # ---- 写回 ----
        layout_path = '/Game/Extraction/Loot/DA_SpaceCenter_Ma1_Loot'
        layout = unreal.load_asset(layout_path)
        # 旧点位是从源关卡 actor 精确导出的，保留；只替换本脚本上一轮写入的 V2_* 点。
        kept = [p for p in layout.get_editor_property('points')
                if not str(p.get_editor_property('id')).startswith('V2_')]
        report['kept_imported_points'] = len(kept)
        # 与源 actor 导出的点去重：3 米内视为同一容器，保留精度更高的旧点。
        kept_xy = []
        for p in kept:
            t = p.get_editor_property('transform').translation
            kept_xy.append((t.x, t.y))
        deduped = []
        for p in loot_points:
            t = p.get_editor_property('transform').translation
            if any(((t.x - kx) ** 2 + (t.y - ky) ** 2) ** 0.5 < 300 for kx, ky in kept_xy):
                continue
            deduped.append(p)
        report['dedup_skipped'] = len(loot_points) - len(deduped)
        loot_points = deduped
        assert loot_points, '去重后没有剩余新增物资点，放弃写入'
        report['loot'] = {'new_total': len(loot_points),
                          'kept_imported': len(kept),
                          'total': len(kept) + len(loot_points)}
        layout.set_editor_property('variant', 'SpaceCenter_Ma1')
        layout.set_editor_property('points', kept + loot_points)
        layout.set_editor_property(
            'group_policy', 'POI anchored, collision validated. Container kinds and drops are project tuning.')
        assert lib.save_loaded_asset(layout), layout_path

        asset = unreal.load_asset(ASSET)
        assert asset, ASSET
        # spawn_groups / exits 不碰：官方点位由 place_official_spawn_exits.py 拥有。
        asset.set_editor_property('loot_layout', layout)
        asset.set_editor_property('source_to_world', unreal.Transform(
            location=unreal.Vector(-ORIGIN_X, -ORIGIN_Y, 22400)))
        bounds = []
        # 整图外框 + 每个主要兴趣点的区域块；战术地图按 Overview 做世界->画布投影，
        # Overview 不覆盖全图时中控、发射区会直接落到画布外（这就是"地图缺区域"的成因）。
        bounds += [unreal.Vector2D(-17500, -28500), unreal.Vector2D(32700, 32300)]
        for _, src in AREA_POIS:
            x, y = world_of(src)
            bounds += [unreal.Vector2D(x - 1800, y - 1800), unreal.Vector2D(x + 1800, y + 1800)]
        asset.set_editor_property('floor_bounds', bounds)
        asset.set_editor_property('overview_min', unreal.Vector2D(-17500, -28500))
        asset.set_editor_property('overview_max', unreal.Vector2D(32700, 32300))
        assert lib.save_loaded_asset(asset), ASSET

        report['loot_new'] = len(loot_points)
        report['spawn_exits_owner'] = 'place_official_spawn_exits.py (未改动)'
        out = Path(unreal.Paths.project_saved_dir()) / 'Extraction/SpaceCenterV2/Report.json'
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print('SPACECENTER_V2_WRITTEN loot_new=%d loot_total=%d dropped=%d (spawn/exits 未改动)' % (
            len(loot_points), report['loot']['total'], len(report['dropped'])))
        for row in report['dropped']:
            print('  DROPPED', row)
    finally:
        levels.load_level(original)


if __name__ == '__main__':
    main()
