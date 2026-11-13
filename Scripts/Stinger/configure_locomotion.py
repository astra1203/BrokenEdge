"""Build opt-in rifle blend spaces and record reversible binding changes."""
import sys as _presentation_sys
from pathlib import Path as _PresentationPath
_presentation_sys.path.insert(0, str(_PresentationPath(__file__).resolve().parents[1] / "Stinger"))
from character_presentation import get_character_property
import json
import math
import struct
import sys
from pathlib import Path
import unreal

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
root = '/Game/Characters/Stinger/Animations/DeltaForceLocomotion'
work = Path(unreal.Paths.project_saved_dir())/'Stinger/Locomotion'
sys.path.insert(0,str(Path(unreal.Paths.project_dir())/'Scripts/Stinger'))
from import_assets import read_psa
manifest = json.loads((work/'Import.json').read_text(encoding='utf-8'))
sequences = {s['name'].lower(): s for e in manifest['entries'].values() for s in e.get('sequences',[])}
report = {'bindings':[], 'calibration':{}, 'assets':[]}

def seq(name):
    s = sequences[name.lower()]
    assert s['validation']=='numeric_passed' and 'Mapped' in s['assets'], name
    result = unreal.load_asset(s['assets']['Mapped'])
    assert result
    # CharacterMovement owns motion; source assets retain the untouched root track.
    result.set_editor_property('enable_root_motion',False)
    result.set_editor_property('force_root_lock',True)
    result.set_editor_property('root_motion_root_lock',unreal.RootMotionRootLock.ANIM_FIRST_FRAME)
    lib.save_loaded_asset(result)
    return result

idle = seq('AR_M_3P_idle')
skeleton = idle.get_editor_property('skeleton')

def speed(name, fallback):
    animation = seq(name)
    psa=next(p for p in (work/'export').rglob('*.psa') if p.stem.lower()==name.lower())
    raw,chunks,bones,infos=read_psa(psa)
    _,fps,first,frames=infos[0]
    start,size,_=chunks['ANIMKEYS']
    a=struct.unpack_from('<3f',raw,start+first*len(bones)*size)
    b=struct.unpack_from('<3f',raw,start+(first+frames-1)*len(bones)*size)
    root_speed=math.dist(a,b)/((frames-1)/fps)
    if root_speed>10:
        report['calibration'][name]={'source_root_speed':root_speed,'blend_speed':round(root_speed),'fallback':False}
        return round(root_speed)
    options = unreal.AnimPoseEvaluationOptions()
    samples = []
    for i in range(41):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(animation,animation.get_play_length()*i/40,options)
        samples.append([unreal.AnimPoseExtensions.get_bone_pose(pose,b,unreal.AnimPoseSpaces.WORLD).translation
                        for b in ('LeftFoot','RightFoot')])
    # During stance, the lower foot travels backwards relative to an in-place body.
    values=[]
    dt=animation.get_play_length()/40
    for a,b in zip(samples,samples[1:]):
        side=0 if a[0].z < a[1].z else 1
        delta=b[side]-a[side]
        if abs(delta.z)/max(dt,.001)<40:
            values.append(math.hypot(delta.x,delta.y)/max(dt,.001))
    values.sort()
    measured=values[len(values)//2] if values else 0
    chosen=round(measured) if 50 <= measured <= 1000 else fallback
    report['calibration'][name]={'stance_speed_estimate':measured,'blend_speed':chosen,'fallback':chosen==fallback}
    return chosen

walk_speed=speed('AR_M_3P_WalkF',150)
run_speed=max(walk_speed+50,speed('AR_M_3P_RunF',600))
crouch_speed=speed('AR_M_3P_crouch_Walk_f',150)
sprint_speed=speed('AR_M_3P_Sprint',561)

def blend(name, samples, max_speed, aim=False):
    path=root+'/BlendSpaces/'+name
    asset=unreal.load_asset(path)
    if not asset:
        factory=unreal.AimOffsetBlendSpaceFactory1D() if aim else unreal.BlendSpaceFactoryNew()
        factory.set_editor_property('target_skeleton',skeleton)
        cls=unreal.AimOffsetBlendSpace1D if aim else unreal.BlendSpace
        asset=tools.create_asset(name,root+'/BlendSpaces',cls,factory)
    assert unreal.StingerAssetLibrary.populate_locomotion_blend_space(asset,[x[0] for x in samples],
        [unreal.Vector(*x[1]) for x in samples],max_speed,aim),name
    assert lib.save_loaded_asset(asset)
    report['assets'].append(path)
    return asset

directions=[(-180,'B'),(-135,'BL'),(-90,'L'),(-45,'FL'),(0,'F'),(45,'FR'),(90,'R'),(135,'BR'),(180,'B')]
standing=[];crouching=[]
for angle,direction in directions:
    standing += [(idle,(angle,0,0)),(seq('AR_M_3P_Walk'+direction),(angle,walk_speed,0)),
                 (seq('AR_M_3P_Run'+direction),(angle,run_speed,0)),
                 (seq('AR_M_3P_Sprint') if abs(angle)<=45 else seq('AR_M_3P_Run'+direction),(angle,850,0))]
    crouching += [(seq('AR_M_3P_crouch_Idle'),(angle,0,0)),
                  (seq('AR_M_3P_crouch_Walk_'+direction),(angle,crouch_speed,0))]
stand=blend('BS_RifleStanding',standing,850)
crouch=blend('BS_RifleCrouching',crouching,crouch_speed)

def aim(name,prefix,center):
    base=seq(center)
    samples=[]
    for pitch,suffix in [(-80,'Down'),(0,None),(80,'Up')]:
        source=seq(prefix+suffix) if suffix else base
        path=root+'/AimV5/'+source.get_name()
        asset=unreal.load_asset(path) or lib.duplicate_asset(source.get_path_name(),path)
        asset.set_editor_property('additive_anim_type',unreal.AdditiveAnimationType.AAT_ROTATION_OFFSET_MESH_SPACE)
        asset.set_editor_property('ref_pose_type',unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME)
        asset.set_editor_property('ref_pose_seq',base)
        asset.set_editor_property('ref_frame_index',0)
        lib.save_loaded_asset(asset)
        samples.append((asset,(pitch,0,0)))
    return blend(name,samples,1,True)

ao=aim('AO_Rifle','AR_M_3P_IdleAO','AR_M_3P_IdleAOCenter')
crouch_ao=aim('AO_RifleCrouching','AR_M_3P_CrouchIdleAO','AR_M_3P_CrouchIdleAOCentet')

def data(name,cls):
    path=root+'/'+name
    asset=unreal.load_asset(path)
    if not asset:
        factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',cls)
        asset=tools.create_asset(name,root,cls,factory)
    return asset

movement=data('DA_RifleLocomotion',unreal.RifleLocomotionSet)
for prop,value in [('standing',stand),('crouching',crouch),('aim_offset',ao),('crouch_aim_offset',crouch_ao),
                   ('walk_speed',float(walk_speed)),('run_speed',float(run_speed)),('crouch_speed',float(crouch_speed)),
                   ('sprint_speed',float(sprint_speed)),('run_threshold',(walk_speed+run_speed)/2)]:
    movement.set_editor_property(prop,value)
for prop,name in [('view_idle','Idle_Pose_2'),('view_walk','Walk_Loop'),('view_run','Run_Loop'),('view_sprint','Sprint_Loop'),
                  ('walk_to_run','WalkToRun'),('run_to_walk','RunToWalk'),('walk_to_sprint','WalkToSprint'),
                  ('run_to_sprint','RunToSprint'),('sprint_to_walk','SprintToWalk')]:
    movement.set_editor_property(prop,seq('AR_M_1P_'+name))
assert lib.save_loaded_asset(movement)

source_bp='/Game/Characters/Stinger/Animations/ThirdPerson/ABP_BrokenEdgeAnimInstance_Stinger'
dest_bp=root+'/ABP_StingerRifleLocomotion'
bp=unreal.load_asset(dest_bp) or lib.duplicate_asset(source_bp,dest_bp)
assert unreal.StingerAssetLibrary.configure_rifle_locomotion(bp), 'WeaponSlot insertion failed'
assert lib.save_loaded_asset(bp)

# Persist old references before changing any character/weapon binding.
character_paths=['/Game/Characters/Stinger/Blueprints/BP_StingerCharacter',
                 '/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired']
characters=[]
for path in character_paths:
    character_bp=unreal.load_asset(path)
    if not character_bp: continue
    cdo=unreal.get_default_object(character_bp.generated_class())
    mesh=get_character_property(cdo, 'mesh')
    old=mesh.get_editor_property('anim_class')
    report['bindings'].append({'asset':path,'property':'mesh.anim_class','before':old.get_path_name() if old else None,'after':dest_bp+'_C'})
    characters.append((character_bp,cdo,mesh))
weapon_bp=unreal.load_asset('/Game/Blueprints/Weapon/BP_AK47')
weapon=unreal.get_default_object(weapon_bp.generated_class())
old_profile=weapon.get_editor_property('presentation')
profile=data('DA_AKLocomotionPresentation',unreal.WeaponPresentation)
assert old_profile is None or old_profile==profile,'Preserve an existing AK presentation before changing it'
report['bindings'].append({'asset':weapon_bp.get_path_name(),'property':'presentation','before':old_profile.get_path_name() if old_profile else None,'after':profile.get_path_name()})
backup=work/'BindingsBefore.json'
if not backup.exists(): backup.write_text(json.dumps(report['bindings'],indent=2),encoding='utf-8')
character=characters[0][1]
profile.set_editor_property('view_fit',get_character_property(character, 'first_person_weapon_fit'))
profile.set_editor_property('view_mesh',get_character_property(character, 'first_person_weapon_visual_mesh'))
profile.set_editor_property('locomotion',movement)
assert lib.save_loaded_asset(profile)
for character_bp,cdo,mesh in characters:
    mesh.set_anim_instance_class(bp.generated_class())
    assert lib.save_loaded_asset(character_bp,only_if_is_dirty=False)
weapon.set_editor_property('presentation',profile)
assert lib.save_loaded_asset(weapon_bp)
(work/'Configuration.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('LOCOMOTION_CONFIGURED',json.dumps(report['calibration']))
