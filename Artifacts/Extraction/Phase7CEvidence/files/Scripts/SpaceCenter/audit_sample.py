"""Cold-load generated scene and validate geometry, transforms and material bindings.

Commandlets do not provide the PIE collision scene; traversal proves floor contact.
"""
import json
import os
from pathlib import Path
import traceback
import unreal

WORK = Path(unreal.Paths.project_saved_dir()) / 'SpaceCenter'
OUT = Path(os.environ['SPACECENTER_RUN_DIR'])
report = {'checks': [], 'actors': [], 'collision_validation': 'See rendered PIE traversal reports'}


def check(name, value, detail=None):
    report['checks'].append({'name': name, 'passed': bool(value), 'detail': detail})


def vec(v):
    return [v.x, v.y, v.z]


try:
    manifest = json.loads((WORK / 'ImportManifest.json').read_text(encoding='utf-8'))
    editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert editor.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterSample')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    generated = {a.get_actor_label(): a for a in actors if a.get_actor_label().startswith('SC_')}
    placement_count = sum(str(a.get_folder_path()) == 'SpaceCenter/Generated' for a in actors)
    check('generated_actor_count', placement_count == len(manifest['actors']) and all('SC_' + a['id'] in generated for a in manifest['actors']), placement_count)
    check('stinger_game_mode', 'BP_StingerTestGameMode' in str(world.get_world_settings().get_editor_property('default_game_mode')))
    imported = json.loads((WORK / 'ImportedAssets.json').read_text(encoding='utf-8'))
    overrides = json.loads((Path(unreal.Paths.project_dir()) / 'Scripts/SpaceCenter/overrides.json').read_text(encoding='utf-8'))
    for entry in json.loads((WORK / 'GeometryAudit.json').read_text(encoding='utf-8'))['checks']:
        mesh = unreal.load_asset(imported['mesh:' + entry['source']]['asset'])
        bounds = mesh.get_bounds()
        low = vec(bounds.origin - bounds.box_extent)
        high = vec(bounds.origin + bounds.box_extent)
        error = max(abs(a - b) for a, b in zip(low + high, entry['min_cm'] + entry['max_cm']))
        check('mesh_bounds_' + manifest['meshes'][entry['source']]['id'], error < 1, {'max_error_cm': error})
        check('two_sided_collision_' + manifest['meshes'][entry['source']]['id'], mesh.get_editor_property('body_setup').get_editor_property('double_sided_geometry'))
    for item in manifest['actors']:
        actor = generated.get('SC_' + item['id'])
        if actor is None:
            continue
        expected = [item['transform']['Translation'][k] - manifest['origin'][i] for i, k in enumerate(['X', 'Y', 'Z'])]
        expected = [a + b for a, b in zip(expected, overrides.get('translation_offsets_cm', {}).get(item['id'], [0, 0, 0]))]
        location = vec(actor.get_actor_location())
        check('position_' + item['id'], max(abs(a - b) for a, b in zip(location, expected)) < .1)
        q = item['transform']['Rotation']
        actual_q = actor.get_actor_transform().rotation
        dot = abs(sum(q[k] * getattr(actual_q, k.lower()) for k in ['X', 'Y', 'Z', 'W']))
        check('rotation_' + item['id'], abs(1 - dot) < .00001)
        expected_scale = [item['transform']['Scale3D'][k] for k in ['X', 'Y', 'Z']]
        check('scale_' + item['id'], max(abs(a - b) for a, b in zip(vec(actor.get_actor_scale3d()), expected_scale)) < .0001)
        mesh = actor.static_mesh_component.static_mesh
        check('mesh_' + item['id'], mesh is not None and mesh.get_path_name() == imported['mesh:' + item['mesh']]['asset'])
        check('materials_' + item['id'], all(actor.static_mesh_component.get_material(i).get_name() == 'MI_' + manifest['materials'][p]['id']
                                             for i, p in enumerate(item['materials']) if p))
        center, extent = actor.get_actor_bounds(False)
        report['actors'].append({'id': item['id'], 'source_mesh': item['mesh'], 'location': location,
                                 'center': vec(center), 'extent': vec(extent), 'collision': str(actor.static_mesh_component.get_collision_enabled())})
    report['result'] = 'passed' if all(c['passed'] for c in report['checks']) else 'failed'
except Exception:
    report.update(result='failed', error=traceback.format_exc())
    unreal.log_error(report['error'])
(OUT / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
if report['result'] != 'passed':
    raise RuntimeError('SpaceCenter cold audit failed')
