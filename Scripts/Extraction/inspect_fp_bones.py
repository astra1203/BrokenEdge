from pathlib import Path
import unreal
root = Path(unreal.Paths.project_dir())
bp = unreal.load_asset('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter')
gen = unreal.get_default_object(bp.generated_class())
mesh = gen.get_editor_property('first_person_mesh_asset')
lines = ['FP_MESH %s' % mesh.get_path_name()]
sk = mesh.get_editor_property('skeleton')
lines.append(unreal.StingerAssetLibrary.describe_skeleton_bones(sk))
out = root / 'Saved/Extraction/FirstPersonBones.txt'
out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
unreal.log_warning('FP_BONES_DUMPED')
