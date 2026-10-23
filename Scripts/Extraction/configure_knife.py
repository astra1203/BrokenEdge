"""Import a basic straight knife candidate; source does not establish the official default knife."""
import json
import sys
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
sys.path.insert(0,str(root/'Scripts/Stinger'))
import repair_import as decoder
decoder.SOURCE = root/'Saved/Extraction/SpaceCenterExpansionSources01'
decoder.DEST = '/Game/Extraction/Weapons/Knife'
lib = unreal.EditorAssetLibrary
manifest = json.loads((decoder.SOURCE/'Manifest.json').read_text(encoding='utf-8'))
names = ['1P_Idle_Pose','1P_Fire_01','1P_Fire_02','3P_Fire_01','3P_Fire_02']
results = {}
sequences = {}
for suffix in names:
    name = 'Melee_M_Knife_FairbairnSykes_'+suffix
    path = next(decoder.SOURCE/p for p in manifest['animations'] if Path(p).stem == name)
    result = decoder.import_file(path)
    assert result['coverage'] >= .9 and 'Mapped' in result['sequences'][0]['assets'],name
    results[name] = result
    sequences[suffix] = unreal.load_asset(result['sequences'][0]['assets']['Mapped'])
meshes = [unreal.load_asset(p) for p in lib.list_assets('/Game/Extraction/Weapons/SourceCandidates',recursive=True)
          if '/Knife_FairbairnSykes_001_1P/' in p and '/SkeletalMeshes/' in p]
mesh = next(m for m in meshes if isinstance(m,unreal.SkeletalMesh))
montages = {}
for field,suffix in [('light_view','1P_Fire_01'),('heavy_view','1P_Fire_02'),
                     ('light_body','3P_Fire_01'),('heavy_body','3P_Fire_02')]:
    montage = unreal.StingerAssetLibrary.create_visual_montage(sequences[suffix],decoder.DEST+'/Montages/AM_'+suffix)
    assert montage and lib.save_loaded_asset(montage)
    montages[field] = montage
for name in ['BP_StingerCharacter','BP_StingerCharacter_Repaired']:
    bp = unreal.load_asset('/Game/Characters/Stinger/Blueprints/'+name)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    melee = unreal.get_default_object(bp.generated_class()).get_editor_property('melee')
    melee.set_editor_property('mesh',mesh)
    melee.set_editor_property('idle',sequences['1P_Idle_Pose'])
    melee.set_editor_property('hand_offset',unreal.Transform(rotation=unreal.Rotator(yaw=-90)))
    for field,value in montages.items(): melee.set_editor_property(field,value)
    assert lib.save_loaded_asset(bp,only_if_is_dirty=False)
(root/'Saved/Extraction/SpaceCenterExpansion01/KnifeAnimations.json').write_text(
    json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('KNIFE_CONFIGURED',mesh.get_path_name())
