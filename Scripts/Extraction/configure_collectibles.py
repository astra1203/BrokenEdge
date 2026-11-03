"""Create the Space Center collectible item assets and turn on recipe pools.

Run `generate_collectibles.py` first (and rebuild, since it rewrites the whitelist), then:

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/configure_collectibles.py" -NullRHI -unattended -nosound

Items are `DA_Item_Col_<official id>`, Kind=Valuable, a cube world mesh (official prop
models are not imported). Pools: every recipe below draws extra collectibles each raid,
weighted by quality; fixed recipe items stay as they were. Idempotent.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir())
DATA = ROOT / 'Scripts/Extraction/data/spacecenter_collectibles.json'
FOLDER = '/Game/Extraction/Items'
LOOT = '/Game/Extraction/Loot/'
# recipe -> (min, max rolls, categories ([] = all), quality weights for rarity 0..5)
COMMON = [40, 30, 16, 9, 4, 1]
RICH = [10, 20, 25, 22, 15, 8]
POOLS = {
    'DA_Project7FValuableRecipe': (2, 4, [], COMMON),
    'DA_Project7FSupplyRecipe': (0, 2, ['Tools', 'Household', 'Misc'], COMMON),
    'DA_Project7FMedicalRecipe': (0, 2, ['Medical'], COMMON),
    'DA_Project7FGearRecipe': (0, 1, ['Electronics', 'Tools'], COMMON),
    'DA_ProjectLargeSafeRecipe': (2, 3, [], RICH),
    'DA_ProjectSmallSafeRecipe': (1, 2, [], RICH),
    'DA_ProjectLooseLootRecipe': (1, 1, [], COMMON),
    'DA_ProjectKeyContractRecipe': (2, 3, ['Intel', 'Electronics', 'Collectible'], RICH),
    'DA_ProjectKeyStaffPassageRecipe': (2, 3, [], RICH),
    'DA_ProjectKeyEastBridgeRecipe': (2, 3, [], RICH),
    'DA_ProjectKeyMonitorRecipe': (2, 3, ['Electronics', 'Intel'], RICH),
    'DA_ProjectKeyPresidentRecipe': (3, 4, ['Collectible', 'Intel'], RICH),
    'DA_ProjectKeyCoreRecipe': (3, 4, ['Electronics', 'Collectible'], RICH),
}
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

definition = unreal.load_class(None, '/Script/BrokenEdge.ExtractionItemDefinition')
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class', definition)
mesh = lib.load_asset('/Engine/BasicShapes/Cube')
items = json.loads(DATA.read_text(encoding='utf-8'))['items']
for row in items:
    name = 'DA_Item_' + row['id']
    path = FOLDER + '/' + name
    item = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, FOLDER, definition, factory)
    assert item, path
    description = row['description'] or row['label']
    for prop, value in dict(
            item_id=row['id'], label=row['label'], kind=unreal.ExtractionItemKind.VALUABLE,
            size=unreal.IntPoint(*row['size']), max_stack=row['max_stack'],
            display_rarity=row['rarity'], display_category=row['category'],
            sell_value=row['sell_value'], world_mesh=mesh,
            description='%s（官方 %s，航天基地）' % (description, row['official_id'])).items():
        item.set_editor_property(prop, value)
    assert lib.save_loaded_asset(item, only_if_is_dirty=False), path

report = {}
for recipe_name, (low, high, categories, weights) in POOLS.items():
    path = LOOT + recipe_name
    if not lib.does_asset_exist(path):
        report[recipe_name] = 'missing'
        continue
    recipe = unreal.load_asset(path)
    recipe.set_editor_property('pool_rolls', unreal.IntPoint(low, high))
    recipe.set_editor_property('pool_categories', [unreal.Name(c) for c in categories])
    recipe.set_editor_property('quality_weights', [float(w) for w in weights])
    assert lib.save_loaded_asset(recipe, only_if_is_dirty=False), path
    report[recipe_name] = [low, high, categories]
print('COLLECTIBLES_CONFIGURED', len(items), json.dumps(report, ensure_ascii=False))
