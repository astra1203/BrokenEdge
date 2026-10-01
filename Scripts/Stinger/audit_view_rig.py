"""Inspect the authored camera, hand and weapon anchors before fitting the view rig."""
import unreal, json
from pathlib import Path
root=Path(unreal.Paths.project_saved_dir())/'Stinger'
manifest=json.loads((root/'import_manifest_v3.json').read_text(encoding='utf-8'))
report={}
names=['AR_M_1P_Idle01','AR_M_1P_AimIdle_Pose','AR_M_1P_ChangeClipFull','AR_M_1P_GetWeapon','AR_M_1P_Fire']
for name in names:
    info=next(s for e in manifest['entries'].values() for s in e.get('sequences',[]) if s['name']==name)
    seq=unreal.load_asset(info['assets']['Mapped'])
    samples=[]
    for fraction in [0,.15,.3,.5,.7,.85,1]:
        pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(seq,seq.get_play_length()*fraction,unreal.AnimPoseEvaluationOptions())
        sample={}
        for bone in ['Camera_Root','Camera_Joint','b_IK_Gun','Weapon_RightHand_Joint','RightHand','LeftHand','RightArm','LeftArm']:
            t=unreal.AnimPoseExtensions.get_bone_pose(pose,bone,unreal.AnimPoseSpaces.WORLD)
            sample[bone]={'position':[t.translation.x,t.translation.y,t.translation.z],
                          'quaternion':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w]}
        samples.append(sample)
    report[name]={'path':seq.get_path_name(),'duration':seq.get_play_length(),'samples':samples}
character=unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired'))
report['third_person_montages']={}
for prop in ['reload_montage','swap_montage']:
    montage=character.get_editor_property(prop)
    report['third_person_montages'][prop]={'duration':montage.get_play_length()}
(root/'view_rig_audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('STINGER_VIEW_RIG_AUDIT',json.dumps(report))
