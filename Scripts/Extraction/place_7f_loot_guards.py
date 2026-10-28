"""把 plan_7f_gameplay.py 的规划写入资产：7F 新区域物资箱点位 + 守卫巡逻路线（在编辑器里跑）。

  1. 四种项目配方 DA_Project7F{Supply,Medical,Gear,Valuable}Recipe（项目调参，不是官方掉率）。
  2. DA_SpaceCenter_Ma1_Loot：删掉旧的 7F_* 点后按规划重建；每个点再做一次竖直落地与胶囊复核，
     不能站立的候选直接丢弃（组内至少留一个），同组候选每局按权重选一个刷箱。
  3. L_SpaceCenterExtraction：删掉旧的 ExtractionPatrol_7F_* 后重建 AExtractionPatrolRoute。
     守卫按路线名排序轮流分配（见 AExtractionGameMode::SpawnGuards），原有 4 条路线排在前面。

运行：
    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript -script="Scripts/Extraction/place_7f_loot_guards.py"
幂等：每次整体重建 7F 部分，其他点位和路线不动。
"""
import json
from pathlib import Path
import zlib

import unreal

PLAN = 'Docs/Extraction/RestorationEvidence/Phase7FGameplay/Plan.json'
LAYOUT = '/Game/Extraction/Loot/DA_SpaceCenter_Ma1_Loot'
LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
BOX = ('/Game/Extraction/Loot/Models/props_collect_box_546_3P/props_collect_box_546_3P/'
       'SkeletalMeshes/props_collect_box_546_3P')
PREFIX = '7F_'
ROUTE_PREFIX = 'ExtractionPatrol_7F_'

RECIPES = {
    'Supply': ('补给箱', [('Ammo', 60), ('Ammo76251', 40), ('Bandage', 1), ('Bandage', 1)]),
    'Medical': ('医疗箱', [('Medkit', 1), ('Bandage', 1), ('Bandage', 1), ('Splint', 1)]),
    'Gear': ('装备箱', [('Helmet2', 1), ('Vest2', 1), ('AmmoAP', 30)]),
    'Valuable': ('物资箱', [('Electronics', 2), ('Tools', 1), ('Document', 1)]),
}
# 绷带等 MaxStack=1 的物品一个格位只能放 1 个，多份要拆成多条。
DROP_UP, DROP_DOWN = 150.0, 300.0
STAND_NORMAL_Z = 0.7


def build_recipes(tools, lib):
    out = {}
    for name, (_, stacks) in RECIPES.items():
        path = '/Game/Extraction/Loot/DA_Project7F%sRecipe' % name
        recipe = unreal.load_asset(path) if lib.does_asset_exist(path) else None
        if not recipe:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property('data_asset_class', unreal.ExtractionLootRecipe)
            recipe = tools.create_asset(path.rsplit('/', 1)[-1], '/Game/Extraction/Loot',
                                        unreal.ExtractionLootRecipe, factory)
        items = []
        for item, quantity in stacks:
            stack = unreal.ExtractionLootStack()
            stack.set_editor_property('item', item)
            stack.set_editor_property('quantity', quantity)
            items.append(stack)
        recipe.set_editor_property('items', items)
        assert lib.save_loaded_asset(recipe), path
        out[name] = recipe
    return out


def floor_at(world, location):
    """在规划点上下小窗口内重新落地并检查胶囊；只接受同层地面。"""
    x, y, z = location
    hit = unreal.StingerAssetLibrary.trace_traversal_surface(
        world, unreal.Vector(x, y, z + DROP_UP), unreal.Vector(x, y, z - DROP_DOWN))
    if not hit or len(hit) != 2:
        return None
    point, normal = hit
    if normal.z < STAND_NORMAL_Z:
        return None
    center = point + unreal.Vector(0, 0, 95)
    if not unreal.StingerAssetLibrary.traversal_capsule_clear(world, center, center + unreal.Vector(0, 0, 0.01), 34, 92):
        return None
    return point


def main():
    project = Path(unreal.Paths.project_dir())
    plan = json.loads((project / PLAN).read_text(encoding='utf-8'))
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    lib = unreal.EditorAssetLibrary
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True), '请先停止 PIE'

    recipes = build_recipes(unreal.AssetToolsHelpers.get_asset_tools(), lib)
    box = unreal.load_asset(BOX)
    assert box, BOX
    layout = unreal.load_asset(LAYOUT)
    assert layout, LAYOUT

    assert levels.load_level(LEVEL), LEVEL
    unreal.StingerAssetLibrary.finish_validation_asset_compilation()
    world = editor.get_editor_world()

    kept = [p for p in layout.get_editor_property('points') if not str(p.get_editor_property('id')).startswith(PREFIX)]
    added, dropped, groups = [], [], set()
    for row in plan['containers']:
        point_location = floor_at(world, row['location'])
        if point_location is None:
            dropped.append(row['id'])
            continue
        point = unreal.ExtractionLootPoint()
        point.set_editor_property('id', row['id'])
        point.set_editor_property('group', row['group'])
        point.set_editor_property('weight', 1.0)
        point.set_editor_property('enabled', True)
        point.set_editor_property('kind', 'Supply')
        point.set_editor_property('label', '%s %s' % (row['poi'], RECIPES[row['recipe']][0]))
        point.set_editor_property('transform', unreal.Transform(
            location=point_location, rotation=unreal.Rotator(pitch=0.0, yaw=float(zlib.crc32(row['id'].encode()) % 360), roll=0.0),
            scale=unreal.Vector(1.0, 1.0, 1.0)))
        point.set_editor_property('appearance', box)
        point.set_editor_property('recipe', recipes[row['recipe']])
        point.set_editor_property('reference', '项目布置：7F 探针可达地面，plan_7f_gameplay.py；非官方刷新点')
        added.append(point)
        groups.add(row['group'])
    missing = sorted({row['group'] for row in plan['containers']} - groups)
    assert not missing, ('groups with no standable candidate', missing)
    layout.set_editor_property('points', kept + added)
    assert lib.save_loaded_asset(layout), LAYOUT

    for actor in actors.get_all_level_actors():
        if actor.get_actor_label().startswith(ROUTE_PREFIX):
            actors.destroy_actor(actor)
    for route in plan['routes']:
        first = unreal.Vector(*route['points'][0])
        actor = actors.spawn_actor_from_class(unreal.ExtractionPatrolRoute, first)
        actor.set_actor_label(route['name'])
        actor.set_editor_property('points', [unreal.Vector(*p) for p in route['points']])
    assert unreal.EditorLoadingAndSavingUtils.save_current_level(), LEVEL

    print('PLACE_7F_LOOT_GUARDS containers=%d dropped=%d groups=%d routes=%d kept_other_points=%d'
          % (len(added), len(dropped), len(groups), len(plan['routes']), len(kept)))
    if dropped:
        print('  DROPPED %s' % ', '.join(dropped))


if __name__ == '__main__':
    main()
