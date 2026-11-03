"""在露天区域补「户外物资箱」刷新点（项目自定，在编辑器里跑）。

做法：复用 add_loose_loot.py 的露天探测（地面 + 可站立 + 头顶 25m 敞开），按更大的最小间距取一批点，
相邻两个点编为一组，每局每组随机出一个箱子；箱子内容用独立配方（少量固定补给 + 偏高品质的收集品抽取）。
写进 DA_SpaceCenter_Ma1_Loot，Kind = "FieldCrate"；重复运行会先删掉旧的 FieldCrate 点，结果由固定种子决定。

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/add_field_crates.py" -NullRHI -unattended -nosound
"""
import random
import sys
from pathlib import Path

import unreal

sys.path.insert(0, str(Path(unreal.Paths.project_dir()) / 'Scripts/Extraction'))
import add_loose_loot as loose  # noqa: E402

KIND = 'FieldCrate'
RECIPE_PATH = '/Game/Extraction/Loot/DA_ProjectFieldCrateRecipe'
FIXED = [('Medkit', 1), ('Bandage', 2)]
POOL_ROLLS = (2, 4)
QUALITY_WEIGHTS = [5, 15, 30, 30, 15, 5]
SITE_COUNT = 48          # 候选点数；两两一组，每局刷 SITE_COUNT / 2 个
MIN_SPACING = 4500.0
GRID = 2500.0
SEED = 20261104


def build_recipe(tools, lib):
    recipe = unreal.load_asset(RECIPE_PATH) if lib.does_asset_exist(RECIPE_PATH) else None
    if not recipe:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.ExtractionLootRecipe)
        recipe = tools.create_asset(RECIPE_PATH.rsplit('/', 1)[-1], '/Game/Extraction/Loot',
                                    unreal.ExtractionLootRecipe, factory)
    items = []
    for item, quantity in FIXED:
        stack = unreal.ExtractionLootStack()
        stack.set_editor_property('item', item)
        stack.set_editor_property('quantity', quantity)
        items.append(stack)
    recipe.set_editor_property('items', items)
    recipe.set_editor_property('pool_rolls', unreal.IntPoint(*POOL_ROLLS))
    recipe.set_editor_property('quality_weights', QUALITY_WEIGHTS)
    assert lib.save_loaded_asset(recipe), RECIPE_PATH
    return recipe


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    lib = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    recipe = build_recipe(tools, lib)
    layout = unreal.load_asset(loose.LAYOUT)
    assert layout, loose.LAYOUT

    assert levels.load_level(loose.LEVEL), loose.LEVEL
    unreal.StingerAssetLibrary.finish_validation_asset_compilation()
    world = editor.get_editor_world()
    candidates = []
    y = loose.Y_RANGE[0]
    while y <= loose.Y_RANGE[1]:
        x = loose.X_RANGE[0]
        while x <= loose.X_RANGE[1]:
            spot = loose.ground_at(world, x, y)
            if spot and spot[1]:
                candidates.append(unreal.Vector(spot[0].x, spot[0].y, spot[0].z))
            x += GRID
        y += GRID

    # 和已有容器、散落点保持距离，避免箱子叠在一起。
    existing = [p.get_editor_property('transform').translation for p in layout.get_editor_property('points')
                if KIND not in str(p.get_editor_property('kind'))]
    random.Random(SEED).shuffle(candidates)
    chosen = []
    for point in candidates:
        far = all((point.x - c.x) ** 2 + (point.y - c.y) ** 2 >= MIN_SPACING ** 2 for c in chosen)
        clear = all((point.x - e.x) ** 2 + (point.y - e.y) ** 2 >= 800.0 ** 2 for e in existing)
        if far and clear:
            chosen.append(point)
            if len(chosen) >= SITE_COUNT:
                break
    assert len(chosen) >= 2, '露天候选点不足'
    # 相邻配对：按坐标排序后两两一组，同组两个点在地图上大致相近。
    chosen.sort(key=lambda p: (round(p.x / 5000.0), p.y))

    kept = [p for p in layout.get_editor_property('points') if KIND not in str(p.get_editor_property('kind'))]
    sites = []
    for index, location in enumerate(chosen):
        point = unreal.ExtractionLootPoint()
        point.set_editor_property('id', 'FIELD_CRATE_%02d' % (index + 1))
        point.set_editor_property('group', 'FIELD_CRATE_GROUP_%02d' % (index // 2 + 1))
        point.set_editor_property('weight', 1.0)
        point.set_editor_property('enabled', True)
        point.set_editor_property('kind', KIND)
        point.set_editor_property('label', '户外物资箱')
        point.set_editor_property('transform', unreal.Transform(
            location=location + unreal.Vector(0, 0, 5),
            rotation=unreal.Rotator(pitch=0.0, yaw=float((index * 83) % 360), roll=0.0),
            scale=unreal.Vector(1.0, 1.0, 1.0)))
        point.set_editor_property('recipe', recipe)
        point.set_editor_property('reference', '项目自定：露天探测 + 两点一组随机，见 add_field_crates.py')
        sites.append(point)
    layout.set_editor_property('points', kept + sites)
    assert lib.save_loaded_asset(layout), loose.LAYOUT
    print('FIELD_CRATES_WRITTEN sites=%d groups=%d candidates=%d kept=%d' % (
        len(sites), (len(sites) + 1) // 2, len(candidates), len(kept)))


main()
