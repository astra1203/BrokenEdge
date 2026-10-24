"""Inspect the installed editor animation sampling API and knife skeleton without saving assets."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'Extraction/KnifePoseInspection.json'
bp = unreal.load_asset('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired')
cdo = unreal.get_default_object(bp.generated_class())
melee = cdo.get_editor_property('melee')
mesh = melee.get_editor_property('mesh')
component = unreal.SkeletalMeshComponent()
component.set_skeletal_mesh_asset(mesh)
report = {'mesh':mesh.get_path_name(), 'bones':[str(component.get_bone_name(i)) for i in range(component.get_num_bones())],
          'offset':str(melee.get_editor_property('hand_offset')),
          'animation_libraries':[n for n in dir(unreal) if 'AnimPose' in n or 'AnimationLibrary' in n]}
for name in report['animation_libraries']:
    cls=getattr(unreal,name)
    report[name]={n:str(getattr(cls,n).__doc__) for n in dir(cls) if 'bone_pose' in n or 'anim_pose' in n}
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('KNIFE_POSE_INSPECTED',out)
