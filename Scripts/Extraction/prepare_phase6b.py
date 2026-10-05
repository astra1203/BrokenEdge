"""Prepare two items and a bounded key-room annex without changing source assets."""
import hashlib
import json
from pathlib import Path
import shutil
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
lib = unreal.EditorAssetLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
target = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
map_file = root / 'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap'
backup = root / 'Saved/Extraction/Phase6BBackup/L_SpaceCenterExtraction.umap'
backup.parent.mkdir(parents=True, exist_ok=True)
if not backup.exists():
    shutil.copy2(map_file, backup)

def imported(folder, name):
    value = lib.load_asset(f'/Game/Maps/SpaceCenter/Assets/{folder}/{name}/StaticMeshes/{name}')
    assert isinstance(value, unreal.StaticMesh), name
    return value

door_mesh = imported('7a44c52b93b3be9a', 'SM_H_Customiz_ExperimentalDoor_01_L')
wall_mesh = imported('056a6189cab1a746', 'SM_H_Building_FutureTech_HBoss_01_InterWall_03E')
file_mesh = imported('15fd3e1b7cdc4420', 'SM_H_FutureTech_Furniture_Folder_01_A')
cube = lib.load_asset('/Engine/BasicShapes/Cube')
definition_class = unreal.load_class(None, '/Script/BrokenEdge.ExtractionItemDefinition')
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class', definition_class)
for ident, label, mesh in [('Key', '储藏室钥匙', cube), ('Document', '机密文件', file_mesh)]:
    path = '/Game/Extraction/Items/DA_Item_' + ident
    item = lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset('DA_Item_' + ident, '/Game/Extraction/Items', definition_class, factory)
    for prop, value in dict(item_id=ident, label=label, kind=unreal.ExtractionItemKind.VALUABLE,
                            size=unreal.IntPoint(1, 1), max_stack=1, world_mesh=mesh).items():
        item.set_editor_property(prop, value)
    assert lib.save_loaded_asset(item)

assert levels.load_level(target)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for actor in actors.get_all_level_actors():
    if 'ExtractionStage6B' in [str(t) for t in actor.tags]:
        actors.destroy_actor(actor)

placements = []
center = (3800, 4600, 362)

def fit(component, mesh, size):
    component.set_static_mesh(mesh)
    bounds = mesh.get_bounds()
    extent, origin = bounds.box_extent, bounds.origin
    scale = unreal.Vector(size[0] / (2 * extent.x), size[1] / (2 * extent.y), size[2] / (2 * extent.z))
    component.set_relative_scale3d(scale)
    component.set_relative_location(unreal.Vector(-origin.x * scale.x, -origin.y * scale.y, -origin.z * scale.z), False, False)

def slab(label, position, size, mesh=cube, yaw=0):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position), unreal.Rotator(yaw=yaw))
    actor.set_actor_label('Phase6B_' + label)
    actor.set_editor_property('tags', ['ExtractionStage6B'])
    component = actor.static_mesh_component
    # Keep the actor at the fitted mesh center. Components of a root static mesh
    # cannot carry a separate relative center, so offset the world location.
    component.set_static_mesh(mesh)
    b = mesh.get_bounds()
    scale = unreal.Vector(size[0]/(2*b.box_extent.x), size[1]/(2*b.box_extent.y), size[2]/(2*b.box_extent.z))
    actor.set_actor_scale3d(scale)
    offset = unreal.MathLibrary.rotate_angle_axis(unreal.Vector(b.origin.x*scale.x, b.origin.y*scale.y, b.origin.z*scale.z), yaw, unreal.Vector(0,0,1))
    actor.set_actor_location(unreal.Vector(*position) - offset, False, False)
    component.set_collision_profile_name('BlockAll')
    if mesh == cube and wall_mesh.get_material(0):
        component.set_material(0, wall_mesh.get_material(0))
    actual_origin, actual_extent = actor.get_actor_bounds(False)
    expected_extent = (size[1]/2, size[0]/2, size[2]/2) if yaw == 90 else tuple(v/2 for v in size)
    for actual, expected in zip((actual_origin.x, actual_origin.y, actual_origin.z), position):
        assert abs(actual-expected) < .1, (label, actual_origin, position)
    for actual, expected in zip((actual_extent.x, actual_extent.y, actual_extent.z), expected_extent):
        assert abs(actual-expected) < .1, (label, actual_extent, expected_extent)
    placements.append({'label': label, 'position': position, 'size': size, 'mesh': mesh.get_path_name(),
                       'yaw': actor.get_actor_rotation().yaw, 'bounds_extent': [actual_extent.x,actual_extent.y,actual_extent.z]})
    # The imported InterWall is a frame, not a solid wall. Fill its opening with
    # project-authored backing, inset from the trim to avoid coplanar surfaces.
    if mesh == wall_mesh:
        slab(label + 'Backing', position, (size[0], 20, size[2]), cube, yaw)
    return actor

# Six-meter annex on the existing north forecourt, away from the exit and patrol legs.
x, y, z = center
slab('Floor', (x, y, z-10), (620, 620, 20))
slab('Roof', (x, y, z+310), (620, 620, 20))
slab('Back', (x, y+300, z+150), (620, 30, 300), wall_mesh)
slab('Left', (x-300, y, z+150), (620, 30, 300), wall_mesh, 90)
slab('Right', (x+300, y, z+150), (620, 30, 300), wall_mesh, 90)
slab('FrontLeft', (x-195, y-300, z+150), (230, 30, 300), wall_mesh)
slab('FrontRight', (x+195, y-300, z+150), (230, 30, 300), wall_mesh)
slab('Lintel', (x, y-300, z+295), (160, 30, 30))

door_class = unreal.load_class(None, '/Script/BrokenEdge.ExtractionKeyRoom')
door = actors.spawn_actor_from_class(door_class, unreal.Vector(x, y-300, z+145))
door.set_actor_label('Phase6B_KeyRoom')
door.set_editor_property('tags', ['ExtractionStage6B'])
door.set_editor_property('loot_offset', unreal.Vector(0, 430, -120))
door.set_editor_property('slide', unreal.Vector(180, 0, 0))
fit(door.get_editor_property('door_mesh'), door_mesh, (160, 20, 280))

light = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, z+260))
light.set_actor_label('Phase6B_InteriorLight')
light.set_editor_property('tags', ['ExtractionStage6B'])
light.point_light_component.set_editor_property('intensity', 500.0)
light.point_light_component.set_editor_property('attenuation_radius', 600.0)

for actor in actors.get_all_level_actors():
    if actor.get_actor_label() == 'ExtractionNavigationBounds':
        actor.set_actor_scale3d(unreal.Vector(45, 45, 12))
assert unreal.ExtractionNavigation.build_navigation(world)
assert levels.save_current_level()
report = {'result': 'prepared', 'map': target, 'definitions': ['Key', 'Document'], 'center': center,
          'door': [x, y-300, z+145], 'approach': [x, y-420, z+90], 'cache': [x, y+130, z+25],
          'placements': placements, 'backup_sha256': hashlib.sha256(backup.read_bytes()).hexdigest(),
          'limits': ['Key pickup uses a prototype cube.', 'Annex is project-authored from existing wall, door and folder assets; it is not an original room reconstruction.']}
(root / 'Saved/Extraction/Phase6BAssets.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
unreal.log('EXTRACTION_PHASE6B_ASSETS_PREPARED')
