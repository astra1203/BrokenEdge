"""Cold native-asset audit, separate from live collision and streaming acceptance."""
import json
import os
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
work = root / 'Saved/SpaceCenter/Phase7A'
out = Path(os.environ['EXTRACTION_RUN_DIR'])
manifest = json.loads((work / 'ImportManifest.json').read_text(encoding='utf-8'))
state = json.loads((work / 'ImportedAssets.json').read_text(encoding='utf-8'))
assembly = json.loads((root / 'Saved/Extraction/Phase7AAssets.json').read_text(encoding='utf-8'))
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
stream = unreal.GameplayStatics.get_streaming_level(world, '/Game/Maps/SpaceCenter/L_SpaceCenterNorth')
assert stream and stream.get_loaded_level()
unreal.EditorLevelUtils.set_level_visibility(stream.get_loaded_level(), True, False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
checks = []


def check(name, passed, detail=None):
    checks.append({'name': name, 'passed': bool(passed), 'detail': detail})


def vec(v):
    return [v.x, v.y, v.z]


by_label = {}
for actor in actors:
    by_label.setdefault(actor.get_actor_label(), []).append(actor)
membership = {a['id']: a['resident'] for a in assembly['actors']}
for entry in manifest['actors']:
    found = by_label.get('SC_' + entry['id'], [])
    check('unique_' + entry['id'], len(found) == 1)
    if len(found) != 1:
        continue
    actor = found[0]
    position = [entry['transform']['Translation'][k] - manifest['origin'][i] for i, k in enumerate('XYZ')]
    check('position_' + entry['id'], max(abs(a - b) for a, b in zip(vec(actor.get_actor_location()), position)) < .1)
    q = entry['transform']['Rotation']
    actual = actor.get_actor_transform().rotation
    check('rotation_' + entry['id'], abs(1 - abs(sum(q[k] * getattr(actual, k.lower()) for k in 'XYZW'))) < .00001)
    scale = [entry['transform']['Scale3D'][k] for k in 'XYZ']
    check('scale_' + entry['id'], max(abs(a - b) for a, b in zip(vec(actor.get_actor_scale3d()), scale)) < .0001)
    component = actor.static_mesh_component
    check('mesh_' + entry['id'], component.static_mesh.get_path_name() == state['mesh:' + entry['mesh']]['asset'])
    check('materials_' + entry['id'], all(component.get_material(i) and component.get_material(i).get_name() == 'MI_' + manifest['materials'][p]['id'] for i, p in enumerate(entry['materials']) if p))
    in_north = 'L_SpaceCenterNorth' in actor.get_outer().get_path_name()
    check('membership_' + entry['id'], in_north != membership[entry['id']])
for entry in json.loads((work / 'GeometryAudit.json').read_text(encoding='utf-8'))['checks']:
    mesh = unreal.load_asset(state['mesh:' + entry['source']]['asset'])
    b = mesh.get_bounds()
    error = max(abs(a - b) for a, b in zip(vec(b.origin - b.box_extent) + vec(b.origin + b.box_extent), entry['min_cm'] + entry['max_cm']))
    check('bounds_' + manifest['meshes'][entry['source']]['id'], error < 1, error)
    check('double_sided_' + manifest['meshes'][entry['source']]['id'], mesh.get_editor_property('body_setup').get_editor_property('double_sided_geometry'))
old = json.loads((root / 'Saved/SpaceCenter/ImportManifest.json').read_text(encoding='utf-8'))
check('old_source_ids_preserved', all(len(by_label.get('SC_' + a['id'], [])) == 1 for a in old['actors']))
check('key_room_preserved', len([a for a in actors if a.get_class().get_name() == 'ExtractionKeyRoom']) == 1)
gameplay = [a for a in actors if a.get_class().get_name().startswith('Extraction')
            or a.get_class().get_name() in ['NavMeshBoundsVolume', 'RecastNavMesh']]
check('all_stateful_and_navigation_actors_resident', all('L_SpaceCenterNorth' not in a.get_outer().get_path_name() for a in gameplay))
check('two_resident_supply_crates', all(len(by_label.get('ExtractionNorthSupply_' + str(i), [])) == 1 and 'L_SpaceCenterNorth' not in by_label['ExtractionNorthSupply_' + str(i)][0].get_outer().get_path_name() for i in range(2)))
check('one_region_manager', len(by_label.get('ExtractionNorthStreaming', [])) == 1)
check('four_patrol_routes', len([a for a in actors if a.get_class().get_name() == 'ExtractionPatrolRoute']) == 4)
check('no_initial_client_load', not stream.get_editor_property('initially_loaded') and not stream.get_editor_property('initially_visible'))
report = {'result': 'passed' if all(c['passed'] for c in checks) else 'failed', 'checks': checks,
          'boundary': 'Cold asset audit. Live traversal, navigation, streaming and performance are separate runs.'}
(out / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
assert report['result'] == 'passed', str([c for c in checks if not c['passed']])
unreal.log('EXTRACTION_PHASE7A_AUDIT_PASSED')
