"""Prepare bounded patrol navigation and basic smoke; preserve the phase-1 map."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
material_path = '/Game/Extraction/Abilities/M_ExtractionSmoke'
if not lib.does_asset_exist(material_path):
    material = lib.load_asset(material_path) if lib.does_asset_exist(material_path) else tools.create_asset(
        'M_ExtractionSmoke', '/Game/Extraction/Abilities', unreal.Material, unreal.MaterialFactoryNew())
    edit = unreal.MaterialEditingLibrary
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    # Opaque, two-sided boundary plus the inside HUD haze gives consistent basic occlusion.
    noise = edit.create_material_expression(material, unreal.MaterialExpressionNoise)
    noise.set_editor_property('scale', .012)
    noise.set_editor_property('quality', 1)
    noise.set_editor_property('levels', 2)
    multiply = edit.create_material_expression(material, unreal.MaterialExpressionMultiply)
    multiply.set_editor_property('const_b', .18)
    edit.connect_material_expressions(noise, '', multiply, 'A')
    add = edit.create_material_expression(material, unreal.MaterialExpressionAdd)
    add.set_editor_property('const_b', .32)
    edit.connect_material_expressions(multiply, '', add, 'A')
    edit.connect_material_property(add, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(material)
    assert lib.save_loaded_asset(material)

assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
route_class = unreal.load_class(None, '/Script/BrokenEdge.ExtractionPatrolRoute')
for actor in actors.get_all_level_actors():
    if actor.get_class() == route_class or actor.get_actor_label() == 'ExtractionNavigationBounds':
        actors.destroy_actor(actor)
bounds = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(400, 1300, 1000))
assert bounds
bounds.set_actor_label('ExtractionNavigationBounds')
bounds.set_actor_scale3d(unreal.Vector(32, 40, 12))
assert bounds.get_actor_bounds(False)[1].x > 1000, 'Navigation volume has no brush'
route = json.loads((root / 'Scripts/SpaceCenter/route.json').read_text(encoding='utf-8'))
segments = {s['name']: s['points'] for s in route['segments']}
patrols = [
    [segments['door'][-1], segments['stairs_up'][-1], segments['stairs_down'][-1]],
    [segments['north_exit'][len(segments['north_exit'])//2], segments['north_exit'][-1],
     segments['outside'][len(segments['outside'])//2]],
]
for i, points in enumerate(patrols):
    actor = actors.spawn_actor_from_class(route_class, unreal.Vector(*points[0]))
    actor.set_actor_label(f'ExtractionPatrol_{i}')
    actor.set_editor_property('points', [unreal.Vector(*p) for p in points])
assert unreal.ExtractionNavigation.build_navigation(world), 'Navigation build failed'
assert levels.save_current_level()
report = {'result': 'prepared', 'patrols': patrols, 'bounds': str(bounds.get_actor_bounds(False)),
          'material': material_path, 'note': 'Runtime navigation and visibility require the network suite.'}
(root / 'Saved/Extraction/Phase5Assets.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
unreal.log('EXTRACTION_PHASE5_ASSETS_PREPARED')
