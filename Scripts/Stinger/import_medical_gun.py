"""导入蜂医激素枪（官方 Support_C101_MedicalGun_01）第一/第三人称模型，并记录挂点骨骼。

用法（编辑器关闭）：
  UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript ^
      -script="<绝对路径>/Scripts/Stinger/import_medical_gun.py" -NullRHI -unattended -nosound
源目录可用环境变量 STINGER_MEDICAL_GUN_SOURCE 覆盖。
证据写入 Saved/Stinger/MedicalGun/Import.json。
"""
import json
import os
from pathlib import Path
import unreal

SOURCE = Path(os.environ.get(
    'STINGER_MEDICAL_GUN_SOURCE',
    r'E:\MyFiles\三角洲解包资产\可用资源\DeltaForce\Content\Models\Weapons\MilitaryProps\Support_C101_MedicalGun_01'))
DEST = '/Game/Extraction/Abilities/MedicalGun/'
FILES = {'1P': 'Support_C101_MedicalGun_01_1P.glb', '3P': 'Support_C101_MedicalGun_01_3P.glb'}
# 官方动作把枪挂在手臂骨架的 Weapon_RightHand_Joint 上；角色网格缺这根骨骼时运行期回退到 RightHand。
ATTACH_BONE = 'Weapon_RightHand_Joint'
CHARACTERS = {
    'view_arms': '/Game/Characters/Stinger/Meshes/',
}


def bones(mesh):
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    return [str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]


def skeletal_meshes(folder):
    paths = unreal.EditorAssetLibrary.list_assets(folder, recursive=True)
    return [m for m in (unreal.load_asset(p) for p in paths) if isinstance(m, unreal.SkeletalMesh)]


def main():
    results = {'source': str(SOURCE), 'guns': {}, 'characters': {}}
    for view, name in FILES.items():
        folder = DEST + view
        task = unreal.AssetImportTask()
        task.filename = str(SOURCE / name)
        task.destination_path = folder
        task.automated = True
        task.save = True
        task.replace_existing = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        meshes = skeletal_meshes(folder)
        assert len(meshes) == 1, [m.get_path_name() for m in meshes]
        mesh = meshes[0]
        results['guns'][view] = {'mesh': mesh.get_path_name(), 'bones': bones(mesh),
                                 'bounds': str(mesh.get_bounds())}
    # 列出蜂医所有骨骼网格是否带官方挂点，供 C++ 选择挂接骨骼。
    for mesh in skeletal_meshes(CHARACTERS['view_arms']):
        names = bones(mesh)
        results['characters'][mesh.get_path_name()] = {
            'has_attach_bone': ATTACH_BONE in names, 'has_right_hand': 'RightHand' in names, 'bone_count': len(names)}
    out = Path(unreal.Paths.project_dir()) / 'Saved/Stinger/MedicalGun/Import.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('MEDICAL_GUN_IMPORTED', json.dumps(results, ensure_ascii=False))


main()
