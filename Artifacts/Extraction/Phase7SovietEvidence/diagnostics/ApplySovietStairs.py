import unreal
lib=unreal.EditorAssetLibrary
sml=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary
for name in ('SM_SmallStairSmall','SM_SmallStairBig'):
    mesh=unreal.load_asset('/Game/UVB80/Assets/UVB80/Buildings/Modular/'+name)
    assert unreal.SovietMilitaryBuilder.configure_stair_collision(mesh)
    assert lib.save_loaded_asset(mesh,only_if_is_dirty=False)
    print('SOVIET_STAIR_COLLISION',name)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SovietMilitary/L_SovietMilitaryExtraction')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert unreal.SovietMilitaryBuilder.build_navigation(world)
assert levels.save_current_level()
print('SOVIET_STAIRS_SAVED')
