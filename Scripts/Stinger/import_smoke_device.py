"""Import the official Stinger honeycomb smoke device (Surport_C101_SmokeScreen_M) 1P / 3P meshes.

Source: 可用资源_DeltaForce_Models_009.zip,
Models/Weapons/MilitaryProps/Support_C101_SmokeScreen/Surport_C101_SmokeScreen_M_{1P,3P}.glb.
UExtractionAbilities shows them in the right hand (Weapon_RightHand_Joint) while the smoke
action plays, the same way the stim gun is shown for heal / revive.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Stinger/import_smoke_device.py" -NullRHI -unattended -nosound
"""
import json
import zipfile
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir())
SOURCES = [Path(r'D:\司晨\源代码和素材\三角洲解包资产\网盘上传压缩包\可用资源_DeltaForce_Models_009.zip'),
           Path(r'\\Desktop-m9pa1sf\三角洲解包资产\网盘上传压缩包\可用资源_DeltaForce_Models_009.zip')]
MEMBER = '可用资源/DeltaForce/Content/Models/Weapons/MilitaryProps/Support_C101_SmokeScreen/Surport_C101_SmokeScreen_M_%s.glb'
DEST = '/Game/Extraction/Abilities/SmokeScreen/'
WORK = ROOT / 'Saved/Stinger/SmokeScreen'

archive = zipfile.ZipFile(next(p for p in SOURCES if p.exists()))
WORK.mkdir(parents=True, exist_ok=True)
report = {}
for view in ('1P', '3P'):
    target = WORK / ('Surport_C101_SmokeScreen_M_%s.glb' % view)
    target.write_bytes(archive.read(MEMBER % view))
    task = unreal.AssetImportTask()
    task.filename = str(target)
    task.destination_path = DEST + view
    task.automated = True
    task.save = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    meshes = [m for m in (unreal.load_asset(p) for p in unreal.EditorAssetLibrary.list_assets(DEST + view, recursive=True))
              if isinstance(m, unreal.SkeletalMesh)]
    assert len(meshes) == 1, [m.get_path_name() for m in meshes]
    report[view] = meshes[0].get_path_name()
(WORK / 'Import.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('SMOKE_DEVICE_IMPORTED', json.dumps(report))
