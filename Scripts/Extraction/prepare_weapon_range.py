"""Create a separate disposable training map; do not edit SpaceCenter."""
import unreal

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
folder = '/Game/Extraction/Training'
path = folder + '/L_WeaponRange'
definition_class = unreal.load_class(None, '/Script/BrokenEdge.ExtractionMapDefinition')
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class', definition_class)
definition = unreal.load_asset(folder + '/DA_WeaponRangeMap') if lib.does_asset_exist(folder + '/DA_WeaponRangeMap') else tools.create_asset('DA_WeaponRangeMap', folder, definition_class, factory)
group = unreal.ExtractionSpawnGroup()
group.set_editor_property('id', 'Range')
group.set_editor_property('positions', [unreal.Transform(location=unreal.Vector(0, y, 100)) for y in [0, 300, 600, 900, 1200, 1500]])
group.set_editor_property('reference', 'Project training layout, not official geography')
exit_definition = unreal.ExtractionExitDefinition()
exit_definition.set_editor_property('id', 'UnusedTrainingExit')
exit_definition.set_editor_property('label', '训练不结算')
exit_definition.set_editor_property('transform', unreal.Transform(location=unreal.Vector(-50000, 0, 0)))
definition.set_editor_property('map_id', 'WeaponRange')
definition.set_editor_property('spawn_groups', [group])
definition.set_editor_property('exits', [exit_definition])
assert lib.save_loaded_asset(definition)
if lib.does_asset_exist(path):
    assert levels.load_level(path)
    # This dedicated map is generator-owned; only replace the generated fixture classes.
    generated = (unreal.StaticMeshActor, unreal.TextRenderActor, unreal.PlayerStart,
                 unreal.DirectionalLight, unreal.SkyLight)
    for actor in actors.get_all_level_actors():
        if isinstance(actor, generated):
            actors.destroy_actor(actor)
else:
    assert levels.new_level(path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/BrokenEdge.WeaponRangeGameMode'))
cube = unreal.load_asset('/Engine/BasicShapes/Cube')


def block(label, position, scale):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
    actor.set_actor_label(label)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_collision_profile_name('BlockAll')
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


block('RangeFloor', (9000, 500, -50), (200, 40, 1))
block('ThinWallSweepTest', (250, -700, 150), (.05, 2, 3))
block('MuzzleObstructionTest', (100, -1100, 90), (.1, 2, 1.8))
for i, distance in enumerate([10, 25, 40, 50, 70, 100]):
    y = i * 400
    block(f'Target_{distance}m', (distance * 100, y, 180), (.1, 2, 3.6))
    text = actors.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(distance * 100 - 15, y, 400), unreal.Rotator(pitch=0, yaw=180, roll=0))
    text.text_render.set_text(f'{distance} m')
    text.text_render.set_world_size(60)
    block(f'Lane_{distance}m', (0, y, 1), (.1, 2, .02))
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 100))
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1500), unreal.Rotator(pitch=-55, yaw=-30, roll=0))
light.light_component.set_intensity(8)
actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 1000))
text = actors.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(500, -500, 350), unreal.Rotator(pitch=0, yaw=180, roll=0))
text.text_render.set_text('WEAPON RANGE | AKM / SCAR-H | disposable loadout')
text.text_render.set_world_size(25)
assert levels.save_current_level()
unreal.log('EXTRACTION_WEAPON_RANGE_PREPARED')
