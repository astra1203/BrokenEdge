import unreal, json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).parent))
import import_assets as assets
root=assets.DEST
report={'quat_methods':[m for m in dir(unreal.Quat) if 'inverse' in m or 'rotator' in m], 'transform_methods':[m for m in dir(unreal.Transform) if 'rotation' in m or 'relative' in m]}
report['pose_doc']=unreal.AnimPoseExtensions.get_anim_pose_at_time.__doc__
manifest=assets.load_manifest()
paths=[next(e['assets'][0] for e in manifest['entries'].values() if e['assets'][0].endswith('/AR_M_1P_Idle01')),
       root+'/Animations/ThirdPerson/Idle_Rifle_Hip_Retargeted_Stinger']
for path in paths:
    seq=unreal.load_asset(path)
    if not seq: continue
    pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(seq,0,unreal.AnimPoseEvaluationOptions())
    report[path]={bone:str(unreal.AnimPoseExtensions.get_bone_pose(pose,bone,unreal.AnimPoseSpaces.WORLD)) for bone in ['Root','Hips','Spine2','RightHand','LeftHand','Head']}
base=unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(root+'/Blueprints/BP_StingerCharacter'))
report['camera_boom']={p:str(base.get_editor_property('camera_boom').get_editor_property(p)) for p in ['target_arm_length','relative_location','relative_rotation','socket_offset']}
Path(unreal.Paths.project_saved_dir(),'Stinger','pose_inspection.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('STINGER_POSES_INSPECTED')
