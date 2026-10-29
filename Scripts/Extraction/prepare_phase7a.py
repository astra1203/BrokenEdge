"""Assemble the bounded north region, preserving the original raid and sample."""
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import sys
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
work = root / 'Saved/SpaceCenter/Phase7A'
out = Path(os.environ['EXTRACTION_RUN_DIR'])
manifest = json.loads((work / 'ImportManifest.json').read_text(encoding='utf-8'))
imported = json.loads((work / 'ImportedAssets.json').read_text(encoding='utf-8'))
main = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
north = '/Game/Maps/SpaceCenter/L_SpaceCenterNorth'
lib = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
backup = root / 'Saved/Extraction/Phase7ABackup/L_SpaceCenterExtraction.umap'
if not backup.exists():
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / 'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap', backup)
if not lib.does_asset_exist(north):
    assert levels.new_level(north)
    assert levels.save_current_level()
assert levels.load_level(main)
sys.path.insert(0,str(root/'Scripts/SpaceCenter'))
from restoration_generation_guard import assert_legacy_map_allowed
assert_legacy_map_allowed('prepare_phase7a')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
stream = unreal.GameplayStatics.get_streaming_level(world, north)
if not stream:
    stream = unreal.EditorLevelUtils.add_level_to_world(world, north, unreal.LevelStreamingDynamic)
assert stream
unreal.EditorLevelUtils.set_level_visibility(stream.get_loaded_level(), True, False)
assert levels.set_current_level_by_name('L_SpaceCenterExtraction')
for actor in actors.get_all_level_actors():
    if 'ExtractionStage7A' in [str(tag) for tag in actor.tags]:
        actors.destroy_actor(actor)

report = {'source_components': len(manifest['actors']), 'actors': [], 'placements': [],
          'backup_sha256': hashlib.sha256(backup.read_bytes()).hexdigest(),
          'approximations': ['Source terrain is not restored. North ground follows the source road elevation Z=54cm.',
                            'A project-authored ramp connects the existing Z=362cm platform to the north ground.',
                            'Structural shells and cross-boundary geometry stay resident; north props stream on clients.']}


def tagged(cls, label, position):
    obj = actors.spawn_actor_from_class(cls, unreal.Vector(*position))
    assert obj
    obj.set_actor_label(label)
    obj.set_editor_property('tags', ['ExtractionStage7A'])
    obj.set_folder_path('Extraction/North')
    return obj


for entry in manifest['actors']:
    transform = entry['transform']
    t, q, scale = transform['Translation'], transform['Rotation'], transform['Scale3D']
    position = [t[k] - manifest['origin'][i] for i, k in enumerate('XYZ')]
    mesh = lib.load_asset(imported['mesh:' + entry['mesh']]['asset'])
    assert mesh
    bounds = mesh.get_bounds()
    row_y = [2 * (q['X'] * q['Y'] + q['Z'] * q['W']),
             1 - 2 * (q['X'] ** 2 + q['Z'] ** 2), 2 * (q['Y'] * q['Z'] - q['X'] * q['W'])]
    center_y = position[1] + sum(row_y[i] * scale[k] * getattr(bounds.origin, k.lower()) for i, k in enumerate('XYZ'))
    extent_y = sum(abs(row_y[i] * scale[k]) * getattr(bounds.box_extent, k.lower()) for i, k in enumerate('XYZ'))
    resident = center_y - extent_y < 6100 or any(s in entry['mesh'] for s in ('Building', 'RoadFacility', 'Ground', 'Stairs', 'Door'))
    assert levels.set_current_level_by_name('L_SpaceCenterExtraction' if resident else 'L_SpaceCenterNorth')
    obj = tagged(unreal.StaticMeshActor, 'SC_' + entry['id'], position)
    obj.set_actor_rotation(unreal.Quat(q['X'], q['Y'], q['Z'], q['W']).rotator(), False)
    obj.set_actor_scale3d(unreal.Vector(*[scale[k] for k in 'XYZ']))
    component = obj.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    body = entry.get('properties', {}).get('BodyInstance', {})
    collision = body.get('CollisionProfileName') != 'NoCollision' and body.get('CollisionEnabled') != 'ECollisionEnabled::NoCollision'
    component.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    translucent = False
    for i, material in enumerate(entry['materials']):
        if not material:
            continue
        asset = lib.load_asset('/Game/Maps/SpaceCenter/Materials/MI_' + manifest['materials'][material]['id'])
        assert asset
        component.set_material(i, asset)
        translucent |= asset.get_base_material().get_editor_property('blend_mode') == unreal.BlendMode.BLEND_TRANSLUCENT
    component.set_editor_property('disallow_nanite', translucent)
    center, extent = obj.get_actor_bounds(False)
    assert abs(center_y - center.y) < 1 and abs(extent_y - extent.y) < 1
    report['actors'].append({'id': entry['id'], 'source': entry['source_object'], 'mesh': entry['mesh'],
                             'position': position, 'resident': resident,
                             'center': [center.x, center.y, center.z], 'extent': [extent.x, extent.y, extent.z]})

assert levels.set_current_level_by_name('L_SpaceCenterExtraction')
cube = lib.load_asset('/Engine/BasicShapes/Cube')
ground_material = lib.load_asset('/Game/Maps/SpaceCenter/Materials/MI_SC_RebuiltGround')


def solid(label, position, size):
    obj = tagged(unreal.StaticMeshActor, label, position)
    obj.static_mesh_component.set_static_mesh(cube)
    obj.static_mesh_component.set_material(0, ground_material)
    obj.static_mesh_component.set_collision_profile_name('BlockAll')
    obj.set_actor_scale3d(unreal.Vector(*[s / 100 for s in size]))
    report['placements'].append({'label': label, 'position': position, 'size': size})
    return obj


solid('SC_NorthRebuiltGround', [1000, 9000, 29], [12000, 6000, 50])
length = math.hypot(1500, 308)
ramp = solid('SC_NorthAccessRamp', [1400, 6250 - 25 * 308 / length, 208 - 25 * 1500 / length], [600, length, 50])
ramp.set_actor_rotation(unreal.MathLibrary.make_rot_from_y(unreal.Vector(0, 1500, -308)), False)
for label, pos, size in [('North', [1000, 12000, 154], [12000, 30, 200]),
                         ('West', [-5000, 9000, 154], [30, 6000, 200]),
                         ('East', [7000, 9000, 154], [30, 6000, 200])]:
    solid('SC_NorthBoundary' + label, pos, size)
manager = tagged(unreal.load_class(None, '/Script/BrokenEdge.ExtractionRegionStream'), 'ExtractionNorthStreaming', [0, 0, 0])
manager.set_editor_property('region_level', unreal.load_asset(north))
loot_class = unreal.EditorAssetLibrary.load_blueprint_class('/Game/Blueprints/Extraction/BP_ExtractionLootActor')
for i, pos in enumerate([[1700, 7500, 79], [1700, 10100, 79]]):
    loot = tagged(loot_class, 'ExtractionNorthSupply_' + str(i), pos)
    loot.set_editor_property('label', '北侧补给箱 ' + str(i + 1))
route_class = unreal.load_class(None, '/Script/BrokenEdge.ExtractionPatrolRoute')
patrols = [[[1400, 7600, 54], [1400, 10000, 54]],
           [[1400, 5000, 362], [1400, 7600, 54]]]
for i, points in enumerate(patrols):
    patrol = tagged(route_class, 'ExtractionPatrol_' + str(i + 2), points[0])
    patrol.set_editor_property('points', [unreal.Vector(*p) for p in points])
for obj in actors.get_all_level_actors():
    if obj.get_actor_label() == 'ExtractionNavigationBounds':
        obj.set_actor_location(unreal.Vector(1000, 3500, 1000), False, True)
        obj.set_actor_scale3d(unreal.Vector(65, 100, 20))
assert unreal.ExtractionNavigation.build_navigation(world), 'North navigation validation failed'
path = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, unreal.Vector(1400, 5000, 362), unreal.Vector(1400, 10000, 54))
assert path and path.is_valid() and not path.is_partial(), 'No complete north access route'
report['north_route'] = [[p.x, p.y, p.z] for p in path.path_points]
stream.set_editor_property('initially_loaded', False)
stream.set_editor_property('initially_visible', False)
assert levels.save_all_dirty_levels()
report.update(result='prepared', patrols=patrols, streamed=sum(not a['resident'] for a in report['actors']))
(out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
(root / 'Saved/Extraction/Phase7AAssets.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
unreal.log('EXTRACTION_PHASE7A_ASSETS_PREPARED')
