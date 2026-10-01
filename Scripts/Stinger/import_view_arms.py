import unreal
from pathlib import Path
lib=unreal.EditorAssetLibrary
dest='/Game/Characters/Stinger/Meshes/FirstPerson/CappedView'
task=unreal.AssetImportTask()
task.filename=str(Path(unreal.Paths.project_dir())/'RawContent/Stinger/Stinger_CappedViewArms.glb')
task.destination_path=dest;task.automated=True;task.save=True;task.replace_existing=True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh=next(unreal.load_asset(p) for p in lib.list_assets(dest,recursive=True) if isinstance(unreal.load_asset(p),unreal.SkeletalMesh))
body=next(unreal.load_asset(p) for p in lib.list_assets('/Game/Characters/Stinger/Meshes/Gameplay',recursive=True) if isinstance(unreal.load_asset(p),unreal.SkeletalMesh))
assert unreal.StingerAssetLibrary.assign_mesh_skeleton(mesh,body.get_editor_property('skeleton'))
assert lib.save_loaded_asset(mesh,only_if_is_dirty=False)
bp=unreal.load_asset('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired')
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cdo=unreal.get_default_object(bp.generated_class())
cdo.set_editor_property('first_person_mesh_asset',mesh)
# Weapon calibration is configured separately by configure_view_fit.py.
assert lib.save_loaded_asset(bp,only_if_is_dirty=False)
print('STINGER_CAPPED_VIEW_ARMS_CONFIGURED',mesh.get_path_name())
