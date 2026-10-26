"""在露天区域铺「随机散落物资」的候选点（在编辑器里跑）。

做法：
  1. 在关卡上按网格垂直探测，取「有地面 + 站得住 + 头顶 25m 敞开」的点为露天候选。
  2. 按最小间距贪心采样，得到一批分散的候选点（SITE_COUNT 个）。
  3. 写进 DA_SpaceCenter_Ma1_Loot，Kind = "Loose"，共享一个「散落物资池」配方。
     运行时 UExtractionLootLayout::SpawnLoose 每局从这批候选里随机抽 LooseSpawnCount 个，
     每个点再从配方里随机取一种物品 —— 位置和物品都是随机的。

和保险箱的关系：Loose 点不参与 Select()，不会占用任何容器位（见 ExtractionLootLayout.cpp）。

运行（编辑器 Output Log，Cmd 模式）：
    py "D:/Users/SCSJ/BrokenEdge2.0/Scripts/Extraction/add_loose_loot.py"

幂等：每次删掉旧的 Loose 点重建，候选顺序由固定种子决定，重复跑结果一致。
"""
import math
from pathlib import Path

import unreal

LAYOUT = '/Game/Extraction/Loot/DA_SpaceCenter_Ma1_Loot'
LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
RECIPE_PATH = '/Game/Extraction/Loot/DA_ProjectLooseLootRecipe'
LOOSE_KIND = 'Loose'

# 散落物资池：每个点从这张表里随机取一条（物品, 数量）。运行时按 Seed 抽，不在这里定。
POOL = [('Ammo', 30), ('AmmoAP', 20), ('Ammo76251', 30), ('Bandage', 2), ('Splint', 1),
        ('Medkit', 1), ('Electronics', 1), ('Tools', 1), ('Document', 1)]

SITE_COUNT = 40             # 候选点总数；实际每局刷 LooseSpawnCount 个（布局上默认 10）
MIN_SPACING = 1500.0        # 候选点之间的最小平面间距（cm）
GRID = 2000.0               # 网格步长（cm）
X_RANGE = (-4000.0, 14000.0)
Y_RANGE = (-30000.0, 4000.0)
SAMPLE_SEED = 20261026      # 只用来自候选排序，保证幂等

ROOF_UP, ROOF_STOP = 2500.0, 200.0     # 头顶探测：地面之上 25m → 2m，无命中即露天
GROUND_UP, GROUND_DOWN = 800.0, 1200.0
STAND_NORMAL_Z = 0.7                   # 坡度门槛，比这更斜就不是可走地面


def build_recipe(tools, lib):
    recipe = unreal.load_asset(RECIPE_PATH) if lib.does_asset_exist(RECIPE_PATH) else None
    if not recipe:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.ExtractionLootRecipe)
        recipe = tools.create_asset(RECIPE_PATH.rsplit('/', 1)[-1], '/Game/Extraction/Loot',
                                    unreal.ExtractionLootRecipe, factory)
    items = []
    for item, quantity in POOL:
        stack = unreal.ExtractionLootStack()
        stack.set_editor_property('item', item)
        stack.set_editor_property('quantity', quantity)
        items.append(stack)
    recipe.set_editor_property('items', items)
    assert lib.save_loaded_asset(recipe), RECIPE_PATH
    return recipe


def ground_at(world, x, y):
    """竖直向下找可站立地面；返回 (location, 是否露天)，失败返回 None。"""
    top = unreal.Vector(x, y, GROUND_UP)
    bottom = unreal.Vector(x, y, -GROUND_DOWN)
    hit = unreal.StingerAssetLibrary.trace_traversal_surface(world, top, bottom)
    if not hit or len(hit) != 2:
        return None
    location, normal = hit
    if normal.z < STAND_NORMAL_Z:
        return None
    center = location + unreal.Vector(0, 0, 95)
    if not unreal.StingerAssetLibrary.traversal_capsule_clear(
            world, center, center + unreal.Vector(0, 0, 0.01), 34, 92):
        return None
    roof = unreal.StingerAssetLibrary.trace_traversal_surface(
        world, location + unreal.Vector(0, 0, ROOF_UP), location + unreal.Vector(0, 0, ROOF_STOP))
    return location, not (roof and len(roof) == 2)


def sample(candidates):
    """贪心采样：先按 (x,y) 排序打散，再保证最小间距。"""
    ordered = sorted(candidates, key=lambda p: (round(p.x / 100.0), round(p.y / 100.0)))
    chosen = []
    for point in ordered:
        if all((point.x - c.x) ** 2 + (point.y - c.y) ** 2 >= MIN_SPACING ** 2 for c in chosen):
            chosen.append(point)
            if len(chosen) >= SITE_COUNT:
                break
    return chosen


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    lib = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True), '请先停止 PIE'

    recipe = build_recipe(tools, lib)
    layout = unreal.load_asset(LAYOUT)
    assert layout, LAYOUT

    original = editor.get_editor_world().get_path_name().split('.')[0]
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    candidates, indoor = [], 0
    try:
        assert levels.load_level(LEVEL), LEVEL
        unreal.StingerAssetLibrary.finish_validation_asset_compilation()
        world = editor.get_editor_world()
        y = Y_RANGE[0]
        while y <= Y_RANGE[1]:
            x = X_RANGE[0]
            while x <= X_RANGE[1]:
                spot = ground_at(world, x, y)
                if spot:
                    location, open_sky = spot
                    if open_sky:
                        candidates.append(unreal.Vector(location.x, location.y, location.z))
                    else:
                        indoor += 1
                x += GRID
            y += GRID
    finally:
        levels.load_level(original)

    chosen = sample(candidates)
    assert chosen, '一个露天点都没找到，检查关卡是否加载了地形碰撞'

    def is_loose(point):
        return LOOSE_KIND in str(point.get_editor_property('kind'))

    kept = [p for p in layout.get_editor_property('points') if not is_loose(p)]
    sites = []
    for index, location in enumerate(chosen):
        point = unreal.ExtractionLootPoint()
        point.set_editor_property('id', 'LOOSE_%02d' % (index + 1))
        point.set_editor_property('weight', 1.0)
        point.set_editor_property('enabled', True)
        point.set_editor_property('kind', LOOSE_KIND)
        point.set_editor_property('label', '地面物资')
        point.set_editor_property('transform', unreal.Transform(
            location=location,
            rotation=unreal.Rotator(pitch=0.0, yaw=float((index * 137) % 360), roll=0.0),
            scale=unreal.Vector(1.0, 1.0, 1.0)))
        point.set_editor_property('recipe', recipe)
        point.set_editor_property('reference', '露天网格探测（地面+可站立+头顶25m无遮挡），候选池')
        sites.append(point)

    layout.set_editor_property('points', kept + sites)
    try:
        # 属性在 C++ 里声明；未编译的旧 DLL 上取不到，这里只提示不中断。
        if not layout.get_editor_property('loose_spawn_count'):
            layout.set_editor_property('loose_spawn_count', 10)
        loose_count = layout.get_editor_property('loose_spawn_count')
    except Exception as exc:
        loose_count = 'MISSING'
        print('LOOSE_SPAWN_COUNT_MISSING 需要先编译 C++（ExtractionLootLayout.h 新增了该属性）：%s' % exc)
    layout.set_editor_property(
        'group_policy',
        str(layout.get_editor_property('group_policy')) +
        ' 露天散落物资 = 网格探测候选池，每局按 LooseSpawnCount 随机抽点、每点随机取一件物品。')
    assert lib.save_loaded_asset(layout), LAYOUT

    print('LOOSE_SITES_WRITTEN layout=%s kept=%d sites=%d (grid candidates=%d indoor skipped=%d) '
          'loose_spawn_count=%s' % (LAYOUT, len(kept), len(sites), len(candidates), indoor, loose_count))
    for index, location in enumerate(chosen[:12]):
        print('  LOOSE_%02d  X=%9.1f Y=%9.1f Z=%8.1f' % (index + 1, location.x, location.y, location.z))
    if len(chosen) > 12:
        print('  ... 共 %d 个候选' % len(chosen))


if __name__ == '__main__':
    main()
