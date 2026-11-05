"""Bake a north-up overview including terrain, then bind it to the map definition."""
import unreal
from pathlib import Path
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SovietMilitary/L_SovietMilitaryExtraction')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
definition=unreal.load_asset('/Game/Extraction/Maps/DA_SovietMilitaryGameplay')
mn=definition.get_editor_property('overview_min');mx=definition.get_editor_property('overview_max')
out=Path(unreal.Paths.project_saved_dir())/'Extraction/SovietMilitary/Overview.png'
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
assert unreal.StingerAssetLibrary.render_level_overview(world,mn,mx,2048,str(out))>0
task=unreal.AssetImportTask()
task.filename=str(out);task.destination_path='/Game/Extraction/Maps';task.destination_name='T_SovietMilitaryOverview'
task.automated=True;task.replace_existing=True;task.save=True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
texture=unreal.load_asset('/Game/Extraction/Maps/T_SovietMilitaryOverview')
texture.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property('srgb',True)
unreal.EditorAssetLibrary.save_loaded_asset(texture)
definition.set_editor_property('overview_texture',texture)
definition.set_editor_property('overview_texture_min',mn)
definition.set_editor_property('overview_texture_max',mx)
assert unreal.EditorAssetLibrary.save_loaded_asset(definition)
print('SOVIET_OVERVIEW_COMPLETE')
