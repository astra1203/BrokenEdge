import unreal,json,math,sys
from pathlib import Path
root='/Game/Characters/Stinger'
lib=unreal.EditorAssetLibrary
manifest=json.loads(Path(unreal.Paths.project_saved_dir(),'Stinger','import_manifest_v3.json').read_text(encoding='utf-8'))
def animation(name):
    sequence=next(s for e in manifest['entries'].values() for s in e.get('sequences',[]) if s['name']==name)
    assert sequence['validation']=='numeric_passed' and 'Mapped' in sequence['assets'], name
    return unreal.load_asset(sequence['assets']['Mapped'])
def duplicate(source,dest):
    if not lib.does_asset_exist(dest): assert lib.duplicate_asset(source,dest),dest
    return unreal.load_asset(dest)
fp=duplicate(root+'/Animations/FirstPerson/ABP_StingerFirstPerson',root+'/Animations/Corrected/ABP_StingerFirstPerson_Repaired')
unreal.BlueprintEditorLibrary.compile_blueprint(fp)
cdo=unreal.get_default_object(fp.generated_class())
for prop,name in [('idle_animation','AR_M_1P_Idle01'),('aim_animation','AR_M_1P_AimIdle_Pose'),('sprint_animation','AR_M_1P_RelaxIdle')]:
    cdo.set_editor_property(prop,animation(name))
cdo.set_editor_property('jump_animation',None)
assert lib.save_loaded_asset(fp)
bp=duplicate(root+'/Blueprints/BP_StingerCharacter',root+'/Blueprints/BP_StingerCharacter_Repaired')
assert unreal.BlueprintEditorLibrary.get_blueprint_parent_class(bp).get_name()=='BrokenEdgeCharacter'
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
character=unreal.get_default_object(bp.generated_class())
character.set_editor_property('first_person_anim_class',fp.generated_class())
character.set_editor_property('first_person_mesh_transform',unreal.Transform(location=unreal.Vector(0,0,-165),rotation=unreal.Rotator(yaw=-90)))
character.set_editor_property('first_person_aim_mesh_offset',unreal.Vector(55,1,9))
character.set_editor_property('first_person_weapon_transform',unreal.Transform())
old_mesh=next(unreal.load_asset(p) for p in lib.list_assets(root+'/Meshes/FirstPerson',recursive=True)
              if p.endswith('/Stinger_1P_Arms.Stinger_1P_Arms'))
mesh=duplicate(old_mesh.get_path_name(),root+'/Meshes/FirstPerson/Repaired/SK_StingerArms_Repaired')
character.set_editor_property('first_person_mesh_asset',mesh)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
weapon=actors.spawn_actor_from_class(lib.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47'),unreal.Vector())
weapon_mesh=weapon.get_editor_property('weapon_mesh')
source_weapon_mesh=weapon_mesh.get_skeletal_mesh_asset()
visual_weapon_mesh=duplicate(source_weapon_mesh.get_path_name(),root+'/Meshes/FirstPerson/Repaired/SK_KA47_Stinger')
bone_transform=weapon_mesh.get_socket_transform('b_gun_Root',unreal.RelativeTransformSpace.RTS_COMPONENT)
for socket,position in [('Hand_R',unreal.Vector(0,-8,0)),('LeftHandSocket',unreal.Vector(0,25,8))]:
    assert abs(bone_transform.scale3d.x-1)<.001 and abs(bone_transform.scale3d.y-1)<.001 and abs(bone_transform.scale3d.z-1)<.001
    offset=unreal.Transform(location=bone_transform.rotation.inversed().rotate_vector(position-bone_transform.translation),rotation=bone_transform.rotation.inversed().rotator())
    unreal.StingerAssetLibrary.add_mesh_socket(visual_weapon_mesh,socket,'b_gun_Root',offset)
assert lib.save_loaded_asset(visual_weapon_mesh)
weapon_mesh.set_skeletal_mesh_asset(visual_weapon_mesh)
character.set_editor_property('first_person_weapon_source_mesh',source_weapon_mesh)
character.set_editor_property('first_person_weapon_visual_mesh',visual_weapon_mesh)
assert weapon_mesh.does_socket_exist('Hand_R') and weapon_mesh.does_socket_exist('LeftHandSocket')
muzzle=weapon_mesh.get_socket_transform('MuzzleFlash',unreal.RelativeTransformSpace.RTS_COMPONENT).translation
grip=weapon_mesh.get_socket_transform('Hand_R',unreal.RelativeTransformSpace.RTS_COMPONENT).translation
actors.destroy_actor(weapon)
pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(animation('AR_M_1P_AimIdle_Pose'),0,unreal.AnimPoseEvaluationOptions())
hand=unreal.AnimPoseExtensions.get_bone_pose(pose,'RightHand',unreal.AnimPoseSpaces.WORLD)
hand_rotation=unreal.Rotator(yaw=-90).quaternion()*hand.rotation
gun_rotation=unreal.Rotator(yaw=-math.degrees(math.atan2(muzzle.y-grip.y,muzzle.x-grip.x))).quaternion()
relative=hand_rotation.inversed()*gun_rotation
unreal.StingerAssetLibrary.add_mesh_socket(mesh,'RightHandSocket','RightHand',unreal.Transform(rotation=relative.rotator(),location=relative.rotate_vector(-grip)))
assert lib.save_loaded_asset(mesh)
for prop,name in [('first_person_fire_montage','AR_M_1P_Fire'),('first_person_reload_montage','AR_M_1P_ChangeClipFull'),('first_person_swap_montage','AR_M_1P_GetWeapon')]:
    montage_path=root+'/Animations/Corrected/MontagesV4/AM_'+name
    montage=unreal.load_asset(montage_path)
    if montage is None:
        montage=unreal.StingerAssetLibrary.create_visual_montage(animation(name),montage_path)
        assert lib.save_loaded_asset(montage)
    character.set_editor_property(prop,montage)
assert lib.save_loaded_asset(bp)
gm=duplicate(root+'/Blueprints/BP_StingerTestGameMode',root+'/Blueprints/BP_StingerRepairGameMode')
unreal.BlueprintEditorLibrary.compile_blueprint(gm)
unreal.get_default_object(gm.generated_class()).set_editor_property('default_pawn_class',bp.generated_class())
assert lib.save_loaded_asset(gm)
level_path=root+'/Maps/L_StingerRepairTest'
duplicate(root+'/Maps/L_StingerTest',level_path)
editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.load_level(level_path)
world=unreal.EditorLevelLibrary.get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode',gm.generated_class())
assert editor.save_current_level()
Path(unreal.Paths.project_saved_dir(),'Stinger','repair_configure_complete.json').write_text(json.dumps({'character':bp.get_path_name(),'parent':'BrokenEdgeCharacter','map':level_path,'visual_weapon':visual_weapon_mesh.get_path_name(),'aim_offset':[55,1,9]}),encoding='utf-8')
print('STINGER_REPAIRED_CHARACTER_CONFIGURED')
