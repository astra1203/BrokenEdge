from pathlib import Path
import unreal
root = Path(unreal.Paths.project_dir())
bp = unreal.load_asset('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter')
gen = unreal.get_default_object(bp.generated_class())
t = gen.get_editor_property('first_person_mesh_transform')
melee = gen.get_editor_property('melee')
out = root / 'Saved/Extraction/FirstPersonTransform.txt'
out.write_text('loc=%s rot=%s scale=%s\nknife_mesh=%s\n' % (
    t.translation, t.rotation, t.scale3d,
    melee.get_editor_property('mesh').get_path_name()), encoding='utf-8')
unreal.log_warning('FP_TRANSFORM_DUMPED')
