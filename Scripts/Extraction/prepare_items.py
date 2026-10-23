"""Create the phase-3 definitions and replace only tagged Extraction loot fixtures.

Run in an isolated UE 5.8.3 editor after building the native classes. The phase-1
map, imported meshes and historical evidence are never modified.
"""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
definition_class = unreal.load_class(None, '/Script/BrokenEdge.ExtractionItemDefinition')
loot_class = unreal.load_class(None, '/Script/BrokenEdge.ExtractionLootActor')
assert definition_class and loot_class
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class', definition_class)
ak = lib.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47')
health_mesh = unreal.load_asset('/Game/sA_PickupSet_1/Models/SM_Pickup_Health')
ammo_mesh = unreal.load_asset('/Game/MilitaryWeapSilver/Weapons/AssaultRifleA_Ammo')
tools_mesh = unreal.load_asset('/Game/Maps/SpaceCenter/Assets/409e475f1d6c58b5/SM_H_FutureTech_Tools_Box_01/StaticMeshes/SM_H_FutureTech_Tools_Box_01')
assert ak and health_mesh and tools_mesh
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
if not isinstance(ammo_mesh, unreal.StaticMesh):
    ammo_mesh = cube
definitions = [
    ('AK', 'AKM 突击步枪', unreal.ExtractionItemKind.WEAPON, (4, 2), 1, None),
    ('Ammo', '步枪弹药', unreal.ExtractionItemKind.AMMO, (1, 1), 60, ammo_mesh),
    ('Medkit', '医疗包', unreal.ExtractionItemKind.MEDICAL, (2, 1), 1, health_mesh),
    ('Electronics', '电子元件', unreal.ExtractionItemKind.VALUABLE, (1, 1), 5, cube),
    ('Tools', '工具组件', unreal.ExtractionItemKind.VALUABLE, (2, 1), 1, tools_mesh),
]
for ident, label, kind, size, stack, mesh in definitions:
    name = 'DA_Item_' + ident
    path = '/Game/Extraction/Items/' + name
    asset = unreal.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, '/Game/Extraction/Items', definition_class, factory)
    assert asset, path
    for key, value in {'item_id': ident, 'label': label, 'kind': kind,
                       'size': unreal.IntPoint(*size), 'max_stack': stack,
                       'use_seconds': 3.0, 'heal_amount': 50.0, 'world_mesh': mesh,
                       'weapon_class': ak if ident == 'AK' else None}.items():
        asset.set_editor_property(key, value)
    assert lib.save_loaded_asset(asset)

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
starts = []
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.PlayerStart) and 'ExtractionSpawn' in [str(t) for t in actor.tags]:
        starts.append(actor.get_actor_location())
    if actor.get_class() == ak or 'ExtractionStage3Loot' in [str(t) for t in actor.tags]:
        actors.destroy_actor(actor)
placements = []

def spawn(label, position, ident=None, quantity=1):
    actor = actors.spawn_actor_from_class(loot_class, unreal.Vector(*position))
    actor.set_actor_label(label)
    actor.set_editor_property('tags', ['ExtractionStage3Loot'])
    actor.set_editor_property('label', label)
    actor.set_editor_property('loose', ident is not None)
    if ident:
        actor.set_editor_property('loose_definition', ident)
        actor.set_editor_property('loose_quantity', quantity)
    placements.append({'label': label, 'position': position, 'item': ident, 'quantity': quantity})
    return actor

for index, position in enumerate(sorted(starts, key=lambda p: (p.x, p.y))):
    spawn('ExtractionAK_%02d' % index, (position.x + 45, position.y, 425), 'AK')
# Keep the two existing combat spawn approaches unobstructed.
spawn('室外补给箱', (1570, 4370, 425))
spawn('室内工具箱', (-1320, -170, 425))
spawn('步枪弹药', (1550, 4110, 425), 'Ammo', 30)
spawn('医疗补给', (-1380, -420, 425), 'Medkit')
spawn('电子元件', (1570, 4470, 425), 'Electronics', 2)
spawn('工具组件', (-1330, -120, 425), 'Tools')
assert levels.save_current_level()
out = root / 'Saved/Extraction/Phase3Assets.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps({'result': 'passed', 'definitions': [d[0] for d in definitions],
                           'placements': placements,
                           'approximations': ['Electronics uses a small cube; no original item icon or medical hand animation is claimed.',
                                              'Supply crates and death containers reuse the imported tools-box mesh.']},
                          ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('EXTRACTION_PHASE3_ASSETS_PREPARED')
