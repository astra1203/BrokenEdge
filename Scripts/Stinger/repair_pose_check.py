import unreal,json,math
from pathlib import Path
out=Path(unreal.Paths.project_saved_dir())/'Stinger'
manifest=json.loads((out/'import_manifest_v3.json').read_text(encoding='utf-8'))
report={}
for entry in manifest['entries'].values():
    for seq_info in entry.get('sequences',[]):
        path=seq_info['assets'].get('Mapped')
        if not path: continue
        sequence=unreal.load_asset(path)
        values=[]
        for fraction in [0,.25,.5,.75,1]:
            pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence,sequence.get_play_length()*fraction,unreal.AnimPoseEvaluationOptions())
            sample={}
            for bone in ['Root','Hips','Head','RightHand','LeftHand','b_IK_Gun']:
                transform=unreal.AnimPoseExtensions.get_bone_pose(pose,bone,unreal.AnimPoseSpaces.WORLD)
                sample[bone]={'position':[transform.translation.x,transform.translation.y,transform.translation.z],
                              'rotation':[transform.rotation.x,transform.rotation.y,transform.rotation.z,transform.rotation.w],
                              'scale':[transform.scale3d.x,transform.scale3d.y,transform.scale3d.z]}
                assert all(math.isfinite(x) for value in sample[bone].values() for x in value)
                assert min(sample[bone]['scale'])>.001, (path,bone,'collapsed scale')
            values.append(sample)
        report[path]={'name':seq_info['name'],'path':path,'samples':values,'numeric_validation':'passed','visual_validation':'pending'}
        if len(report)%20==0: unreal.SystemLibrary.collect_garbage()
(out/'repair_pose_check.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('STINGER_REPAIR_POSES',len(report))
