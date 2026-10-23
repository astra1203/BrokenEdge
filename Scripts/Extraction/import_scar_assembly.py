"""Import and inspect the provisional assembled SCAR meshes in isolation."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
source = root/'Saved/Extraction/SpaceCenterExpansionSources01'
results = {}
for view in ('1P', '3P'):
    folder = '/Game/Extraction/Weapons/SCARH/SocketAssembly'+view
    # This folder is generated exclusively by this script. Keep player-facing
    # assets elsewhere so rebuilding source geometry cannot overwrite tuning.
    task = unreal.AssetImportTask()
    task.filename = str(source/('SCARH_Candidate_'+view+'.glb'))
    task.destination_path = folder
    task.automated = True
    task.save = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    meshes = [unreal.load_asset(p) for p in unreal.EditorAssetLibrary.list_assets(folder, recursive=True)
              if '/SkeletalMeshes/' in p]
    meshes = [m for m in meshes if isinstance(m, unreal.SkeletalMesh)]
    assert len(meshes) == 1, [(m.get_path_name()) for m in meshes]
    mesh = meshes[0]
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    results[view] = {'mesh': mesh.get_path_name(), 'bones': [str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())],
                     'bounds': str(mesh.get_bounds()), 'materials': len(mesh.get_editor_property('materials')),
                     'status': 'imported_provisional_assembly_not_playtested'}
out = root/'Saved/Extraction/SpaceCenterExpansion01/ScarAssembly.json'
out.write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('SCAR_ASSEMBLY_IMPORTED',json.dumps(results,ensure_ascii=False))
