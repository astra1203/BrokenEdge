"""Generate the door's actual simple collider, then rebuild the authored map navigation."""
import unreal

mesh = unreal.load_asset('/Game/UVB80/Assets/UVB80/Buildings/Door/SM_Door')
assert mesh and unreal.SovietMilitaryBuilder.configure_door_collision(mesh)
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SovietMilitary/L_SovietMilitaryExtraction')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert unreal.SovietMilitaryBuilder.build_navigation(world)
assert levels.save_current_level()
unreal.log('SOVIET_DOOR_COLLISION_SAVED')
