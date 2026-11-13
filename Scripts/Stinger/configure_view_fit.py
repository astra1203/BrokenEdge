"""Fit the capped first-person sleeves to AK anchors; promote only after visual review."""
import sys as _presentation_sys
from pathlib import Path as _PresentationPath
_presentation_sys.path.insert(0, str(_PresentationPath(__file__).resolve().parents[1] / "Stinger"))
from character_presentation import get_character_property, set_character_property, save_character_presentation
import unreal, json, sys
from pathlib import Path
root='/Game/Characters/Stinger';lib=unreal.EditorAssetLibrary
saved=Path(unreal.Paths.project_saved_dir())/'Stinger'
manifest=json.loads((saved/'import_manifest_v3.json').read_text(encoding='utf-8'))
def sequence(name):
    s=next(s for e in manifest['entries'].values() for s in e.get('sequences',[]) if s['name']==name)
    assert s['validation']=='numeric_passed'
    return unreal.load_asset(s['assets']['Mapped'])
def duplicate(source,dest):
    if not lib.does_asset_exist(dest): assert lib.duplicate_asset(source,dest),dest
    return unreal.load_asset(dest)
bp=unreal.load_asset(root+'/Blueprints/BP_StingerCharacter_Repaired')
assert unreal.BlueprintEditorLibrary.get_blueprint_parent_class(bp).get_name()=='BrokenEdgeCharacter'
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
character=unreal.get_default_object(bp.generated_class())
fp=unreal.load_asset(root+'/Animations/Corrected/ABP_StingerFirstPerson_Repaired')
unreal.BlueprintEditorLibrary.compile_blueprint(fp)
anim_defaults=unreal.get_default_object(fp.generated_class())
for prop,name in [('idle_animation','AR_M_1P_Idle01'),('aim_animation','AR_M_1P_AimIdle_Pose'),('sprint_animation','AR_M_1P_RelaxIdle')]:
    anim_defaults.set_editor_property(prop,sequence(name))
anim_defaults.set_editor_property('jump_animation',None)
assert lib.save_loaded_asset(fp,only_if_is_dirty=False)
set_character_property(character, 'first_person_anim_class',fp.generated_class())
source_body=next(unreal.load_asset(p) for p in lib.list_assets(root+'/Meshes/Gameplay',recursive=True) if isinstance(unreal.load_asset(p),unreal.SkeletalMesh))
body=next(unreal.load_asset(p) for p in lib.list_assets(root+'/Meshes/FirstPerson/CappedView',recursive=True) if isinstance(unreal.load_asset(p),unreal.SkeletalMesh))
assert lib.save_loaded_asset(body,only_if_is_dirty=False)
set_character_property(character, 'first_person_mesh_asset',body)
set_character_property(character, 'first_person_mesh_transform',unreal.Transform(location=unreal.Vector(20,0,-165),rotation=unreal.Rotator(yaw=-90)))
set_character_property(character, 'first_person_aim_mesh_offset',unreal.Vector())
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
weapon=actors.spawn_actor_from_class(lib.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47'),unreal.Vector())
component=weapon.get_editor_property('weapon_mesh')
source_weapon=component.get_skeletal_mesh_asset()
view_weapon=duplicate(source_weapon.get_path_name(),root+'/Meshes/FirstPerson/ViewFit/SK_KA47_ViewFit')
component.set_skeletal_mesh_asset(view_weapon)
bone=component.get_socket_transform('b_gun_Root',unreal.RelativeTransformSpace.RTS_COMPONENT)
pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence('AR_M_1P_Idle01'),0,unreal.AnimPoseEvaluationOptions())
right=unreal.AnimPoseExtensions.get_bone_pose(pose,'RightHand',unreal.AnimPoseSpaces.WORLD)
left=unreal.AnimPoseExtensions.get_bone_pose(pose,'LeftHand',unreal.AnimPoseSpaces.WORLD)
camera_rotation=unreal.Rotator(yaw=-90).quaternion()
gun_rotation=unreal.Rotator(yaw=-90).quaternion()
for socket,position,hand in [('Hand_R',unreal.Vector(0,-8,0),right),('LeftHandSocket',unreal.Vector(0,25,-2),left)]:
    socket_rotation=gun_rotation.inversed()*camera_rotation*hand.rotation
    offset=unreal.Transform(location=bone.rotation.inversed().rotate_vector(position-bone.translation),
                            rotation=(bone.rotation.inversed()*socket_rotation).rotator())
    unreal.StingerAssetLibrary.add_mesh_socket(view_weapon,socket,'b_gun_Root',offset)
assert lib.save_loaded_asset(view_weapon)
fit=unreal.StingerViewWeaponFit()
fit.set_editor_property('enabled',True)
fit.set_editor_property('hip',unreal.Transform(location=unreal.Vector(40,14,-22),rotation=unreal.Rotator(yaw=-90)))
fit.set_editor_property('aim',unreal.Transform(location=unreal.Vector(20,0,-13.95),rotation=(unreal.Rotator(pitch=.75).quaternion()*unreal.Rotator(yaw=-90).quaternion()).rotator()))
fit.set_editor_property('source_right_hand',right)
fit.set_editor_property('source_left_hand',left)
reload=sequence('AR_M_1P_ChangeClipFull')
for prop,fraction in [('source_left_magazine_out',.12),('source_left_magazine_in',.52)]:
    pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(reload,reload.get_play_length()*fraction,unreal.AnimPoseEvaluationOptions())
    fit.set_editor_property(prop,unreal.AnimPoseExtensions.get_bone_pose(pose,'LeftHand',unreal.AnimPoseSpaces.WORLD))
fit.set_editor_property('magazine_reference',component.get_socket_transform('b_gun_mag',unreal.RelativeTransformSpace.RTS_COMPONENT))
grip=component.get_socket_transform('LeftHandSocket',unreal.RelativeTransformSpace.RTS_COMPONENT)
magazine=component.get_socket_transform('b_gun_mag',unreal.RelativeTransformSpace.RTS_COMPONENT)
fit.set_editor_property('magazine_grip',unreal.Transform(location=magazine.translation+unreal.Vector(-3,0,-8),rotation=fit.get_editor_property('source_left_magazine_out').rotation.rotator()))
fit.set_editor_property('magazine_out',.12)
fit.set_editor_property('magazine_in',.52)
actors.destroy_actor(weapon)
set_character_property(character, 'first_person_weapon_fit',fit)
set_character_property(character, 'first_person_weapon_source_mesh',source_weapon)
set_character_property(character, 'first_person_weapon_visual_mesh',view_weapon)
set_character_property(character, 'first_person_weapon_transform',unreal.Transform())
timings={}
for prop,name in [('first_person_fire_montage','AR_M_1P_Fire'),('first_person_reload_montage','AR_M_1P_ChangeClipFull'),('first_person_swap_montage','AR_M_1P_GetWeapon')]:
    montage=unreal.load_asset(root+'/Animations/Corrected/MontagesV4/AM_'+name)
    assert montage is not None
    montage=unreal.StingerAssetLibrary.create_visual_montage(sequence(name),root+'/Animations/Corrected/MontagesV4/AM_'+name)
    assert lib.save_loaded_asset(montage,only_if_is_dirty=False)
    set_character_property(character, prop,montage)
    description=json.loads(unreal.StingerAssetLibrary.get_montage_description(montage))
    assert not description['notifies'],'First-person montage must be visual only'
    timings[prop]=description
for prop in ['reload_montage','swap_montage','fire_weapon_montage']:
    timings[prop]=json.loads(unreal.StingerAssetLibrary.get_montage_description(get_character_property(character, prop)))
(saved/'view_fit_montage_timings.json').write_text(json.dumps(timings,indent=2),encoding='utf-8')
save_character_presentation(character)
assert lib.save_loaded_asset(bp,only_if_is_dirty=False)
assert get_character_property(character, 'first_person_mesh_asset')==body
assert get_character_property(character, 'first_person_anim_class')==fp.generated_class()
(saved/'view_fit_configuration.json').write_text(json.dumps({'character':bp.get_path_name(),'body':body.get_path_name(),
    'weapon':view_weapon.get_path_name(),'hip':[40,14,-22],'aim':[20,0,-13.95],'aim_pitch':.75,
    'body_offset':[20,0,-165],'magazine_out':.12,'magazine_in':.52,
    'third_person':'existing project retargeted animations'},indent=2),encoding='utf-8')
print('STINGER_VIEW_FIT_CONFIGURED')
