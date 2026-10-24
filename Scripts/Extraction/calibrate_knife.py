"""Derive knife mounts and upper-body hold pose from the source's retargeted animation."""
import json
import sys
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir())
sys.path.insert(0,str(root/'Scripts/Stinger'))
import repair_import as decoder
decoder.SOURCE=root/'Saved/Extraction/SpaceCenterExpansionSources01'
decoder.DEST='/Game/Extraction/Weapons/Knife'
manifest=json.loads((decoder.SOURCE/'Manifest.json').read_text(encoding='utf-8'))
source=next(decoder.SOURCE/p for p in manifest['animations'] if p.endswith('/Melee_M_Knife_FairbairnSykes_3P_Idle_Pose.psa'))
result=decoder.import_file(source)
assert result['coverage'] >= .9
body=unreal.load_asset(result['sequences'][0]['assets']['Mapped'])
lib=unreal.EditorAssetLibrary
report={'body_idle':body.get_path_name(),'source':str(source.relative_to(decoder.SOURCE)), 'characters':[]}
for name in ['BP_StingerCharacter','BP_StingerCharacter_Repaired']:
    bp=unreal.load_asset('/Game/Characters/Stinger/Blueprints/'+name)
    cdo=unreal.get_default_object(bp.generated_class())
    melee=cdo.get_editor_property('melee')
    offsets={}
    view_mount=None
    for field,sequence in [('view_grip_correction',melee.get_editor_property('idle')),('body_grip_correction',body)]:
        pose=unreal.AnimPoseExtensions.get_anim_pose_at_frame(sequence,0,unreal.AnimPoseEvaluationOptions())
        names=[str(n) for n in pose.get_bone_names()]
        assert 'RightHand' in names and 'b_IK_Gun' in names, names
        source_path=sequence.get_path_name().replace('/MappedV4/','/Source/')
        source_sequence=unreal.load_asset(source_path)
        assert source_sequence,source_path
        source_pose=unreal.AnimPoseExtensions.get_anim_pose_at_frame(source_sequence,0,unreal.AnimPoseEvaluationOptions())
        hand=source_pose.get_bone_pose('RightHand',unreal.AnimPoseSpaces.WORLD)
        gun=source_pose.get_bone_pose('b_IK_Gun',unreal.AnimPoseSpaces.WORLD)
        print('KNIFE_SOURCE_GRIP',field,str(hand),str(gun))
        mount=gun.make_relative(hand)
        if field=='body_grip_correction' and mount.translation.length() >= 50:
            # The 3P idle leaves the weapon IK root at origin. Use the verified
            # 1P hand-local grip for this same knife and character skeleton.
            assert view_mount is not None
            mount=view_mount
            offsets['body_source']='same-skeleton 1P hand-local grip; 3P IK root is unanimated'
        assert mount.translation.length() < 50, str(mount)
        if field=='view_grip_correction': view_mount=mount
        offsets[field]=str(mount)
        melee.set_editor_property(field,mount)
        if field=='body_grip_correction':
            hold={n:pose.get_bone_pose(n,unreal.AnimPoseSpaces.LOCAL) for n in names
                  if n.startswith(('Spine','RightShoulder','LeftShoulder','RightArm','RightForeArm','RightHand','LeftArm','LeftForeArm','LeftHand'))}
            assert len(hold)>=20,len(hold)
            melee.set_editor_property('body_hold_pose',hold)
    melee.set_editor_property('hand_offset',unreal.Transform())
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert lib.save_loaded_asset(bp,only_if_is_dirty=False)
    report['characters'].append({'blueprint':bp.get_path_name(),'offsets':offsets,'hold_bones':len(hold)})
out=root/'Saved/Extraction/KnifeCalibration.json'
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('KNIFE_CALIBRATED',out)
