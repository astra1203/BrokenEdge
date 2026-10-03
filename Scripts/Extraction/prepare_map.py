"""Duplicate only the phase-1 map; share its meshes/materials and preserve the original."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
lib = unreal.EditorAssetLibrary
source = '/Game/Maps/SpaceCenter/L_SpaceCenterSample'
target = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
if not lib.does_asset_exist(target):
    assert lib.duplicate_asset(source, target), 'Map duplication failed'
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(target)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
# Translucent imported glass uses its fallback mesh; Nanite only supports the
# opaque parent. Keep this choice when the phase-2 map is regenerated.
for actor in actors.get_all_level_actors():
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        materials = [component.get_material(i) for i in range(component.get_num_materials())]
        if any(m and m.get_base_material().get_editor_property('blend_mode') == unreal.BlendMode.BLEND_TRANSLUCENT for m in materials):
            component.set_editor_property('disallow_nanite', True)
weapon_class = lib.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47')
for actor in actors.get_all_level_actors():
    if isinstance(actor, unreal.PlayerStart) or actor.get_class() == weapon_class:
        actors.destroy_actor(actor)
starts = [(-1500, -300, 600), (1400, 4200, 600), (-1700, -300, 600),
          (-1500, 0, 600), (1400, 3800, 600), (1800, 4200, 600), (2200, 4200, 600), (2200, 3800, 600)]
for index, position in enumerate(starts):
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(*position))
    start.set_actor_label('ExtractionStart_%02d' % index)
    start.set_editor_property('tags', ['ExtractionSpawn'])
    # A deterministic tag lets the mode order starts without relying on generated object names.
    start.set_editor_property('player_start_tag', 'Extraction_%02d' % index)
    weapon = actors.spawn_actor_from_class(weapon_class, unreal.Vector(position[0] + 20, position[1], 406))
    weapon.set_actor_label('ExtractionAK_%02d' % index)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
mode = unreal.load_class(None, '/Script/BrokenEdge.ExtractionGameMode')
assert mode
world.get_world_settings().set_editor_property('default_game_mode', mode)
assert levels.save_current_level()
out = root / 'Saved/Extraction/Map.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps({'result': 'passed', 'source': source, 'map': target, 'game_mode': mode.get_path_name(), 'starts': starts}, indent=2) + '\n', encoding='utf-8')
print('EXTRACTION_MAP_PREPARED')
# Once phase 3 is installed, regeneration must not reintroduce legacy world
# weapons that bypass item identity and inventory ownership.
if lib.does_asset_exist('/Game/Extraction/Items/DA_Item_AK'):
    exec(compile((root / 'Scripts/Extraction/prepare_items.py').read_text(encoding='utf-8'),
                 str(root / 'Scripts/Extraction/prepare_items.py'), 'exec'))
