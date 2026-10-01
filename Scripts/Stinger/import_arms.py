import unreal
from pathlib import Path
dest='/Game/Characters/Stinger/Meshes/FirstPerson/Forearms'
if not unreal.EditorAssetLibrary.list_assets(dest,recursive=True):
    task=unreal.AssetImportTask()
    task.filename=str(Path(unreal.Paths.project_dir())/'RawContent'/'Stinger'/'Stinger_1P_Forearms.glb')
    task.destination_path=dest
    task.automated=True
    task.save=True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
print('STINGER_ARMS_IMPORTED')
