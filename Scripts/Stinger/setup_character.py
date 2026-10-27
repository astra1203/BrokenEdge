"""独立 UE Commandlet 中建立蜂医动画、角色和测试地图。"""
import unreal
import json
import os
import math
from pathlib import Path
import import_assets as assets

ROOT = assets.DEST
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LIB = unreal.EditorAssetLibrary
MESH = assets.gameplay_mesh()
SKELETON = MESH.get_editor_property("skeleton")
BONES = {'root':'Root','pelvis':'Hips','spine_01':'Spine','spine_02':'Spine1','spine_03':'Spine2',
         'neck_01':'Neck','neck_02':'Neck','head':'Head','clavicle_l':'LeftShoulder','clavicle_r':'RightShoulder',
         'upperarm_l':'LeftArm','upperarm_r':'RightArm','lowerarm_l':'LeftForeArm','lowerarm_r':'RightForeArm',
         'hand_l':'LeftHand','hand_r':'RightHand','thigh_l':'LeftUpLeg','thigh_r':'RightUpLeg',
         'calf_l':'LeftLeg','calf_r':'RightLeg','foot_l':'LeftFoot','foot_r':'RightFoot',
         'ball_l':'LeftToeBase','ball_r':'RightToeBase','ik_hand_root':'b_IK_Hand_Root','ik_hand_gun':'b_IK_Gun',
         'ik_hand_l':'b_IK_Hand_L','ik_hand_r':'b_IK_Hand_R','ik_foot_root':'b_IK_Foot_Root',
         'ik_foot_l':'b_IK_Foot_L','ik_foot_r':'b_IK_Foot_R','backpack':'Spine2','blanket':'Spine2'}
for side, prefix in [('l','Left'),('r','Right')]:
    for finger in ['thumb','index','middle','ring','pinky']:
        for index in range(1,4):
            BONES[f'{finger}_{index:02d}_{side}'] = prefix+'Hand'+finger.title()+str(index)


def create(name, folder, cls, factory):
    path = folder+'/'+name
    if LIB.does_asset_exist(path):
        return unreal.load_asset(path)
    return TOOLS.create_asset(name, folder, cls, factory)


def retarget():
    source_mesh = unreal.load_asset('/Game/LearningKit_Games/Assets/Characters/Character/Mesh/SK_EpicCharacter')
    chains = [('Spine','spine_01','spine_03'),('Head','neck_01','head'),
              ('LeftArm','clavicle_l','hand_l'),('RightArm','clavicle_r','hand_r'),
              ('LeftLeg','thigh_l','ball_l'),('RightLeg','thigh_r','ball_r')]
    for side in ['l','r']:
        for finger in ['thumb','index','middle','ring','pinky']:
            chains.append((side+'_'+finger,finger+'_01_'+side,finger+'_03_'+side))
    rigs = []
    for name, mesh, target in [('IK_EpicCharacter_StingerSource',source_mesh,False),('IK_Stinger',MESH,True)]:
        rig = create(name, ROOT+'/Animations/Retarget',unreal.IKRigDefinition,unreal.IKRigDefinitionFactory())
        controller = unreal.IKRigController.get_controller(rig)
        controller.set_skeletal_mesh(mesh)
        controller.set_retarget_root('Hips' if target else 'pelvis')
        existing = {str(x.chain_name) for x in controller.get_retarget_chains()}
        for chain,start,end in chains:
            if chain not in existing:
                controller.add_retarget_chain(chain,BONES[start] if target else start,BONES[end] if target else end,'None')
        LIB.save_loaded_asset(rig)
        rigs.append(rig)
    factory_cls = getattr(unreal,'IKRetargetFactory',None) or getattr(unreal,'IKRetargeterFactory')
    rtg = create('RTG_EpicCharacter_To_Stinger',ROOT+'/Animations/Retarget',unreal.IKRetargeter,factory_cls())
    controller = unreal.IKRetargeterController.get_controller(rtg)
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE,rigs[0])
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET,rigs[1])
    controller.add_default_ops()
    controller.auto_map_chains(unreal.AutoMapChainType.EXACT,True)
    controller.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
    LIB.save_loaded_asset(rtg)
    dest = ROOT+'/Animations/ThirdPerson'
    if not LIB.does_asset_exist(dest+'/ABP_BrokenEdgeAnimInstance_Stinger'):
        source_assets = [LIB.find_asset_data('/Game/Blueprints/Character/Animation/'+name) for name in
                         ['ABP_BrokenEdgeAnimInstance','AM_Reload','AM_FireWeapon','AM_Swap','AM_HitReact','AM_Defeated']]
        result = unreal.IKRetargetBatchOperation.duplicate_and_retarget(source_assets,source_mesh,MESH,rtg,
                    suffix='_Stinger',target_path=dest,include_referenced_assets=True)
        print('RETARGETED',len(result))
    bp = unreal.load_asset(dest+'/ABP_BrokenEdgeAnimInstance_Stinger')
    unreal.StingerAssetLibrary.remap_blueprint_bones(bp,BONES)
    LIB.save_directory(dest,only_if_is_dirty=True,recursive=True)


def find_animation(name):
    for entry in assets.load_manifest()['entries'].values():
        if entry.get('status') == 'success':
            for path in entry['assets']:
                if path.rsplit('/',1)[-1] == name:
                    return unreal.load_asset(path)
    raise RuntimeError('Missing imported animation '+name)


def configure():
    fp_mesh=next(unreal.load_asset(p) for p in LIB.list_assets(ROOT+'/Meshes/FirstPerson/Forearms',recursive=True)
                 if isinstance(unreal.load_asset(p),unreal.SkeletalMesh))
    assert unreal.StingerAssetLibrary.assign_mesh_skeleton(fp_mesh,SKELETON), 'Arm skeleton is incompatible'
    actor_tools=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    reference_weapon=actor_tools.spawn_actor_from_class(LIB.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47'),unreal.Vector())
    weapon_mesh=reference_weapon.get_editor_property('weapon_mesh')
    muzzle=weapon_mesh.get_socket_transform('MuzzleFlash',unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    grip=weapon_mesh.get_socket_transform('Hand_R',unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    assert (muzzle-grip).length()>10, 'Weapon sockets were not initialized'
    actor_tools.destroy_actor(reference_weapon)
    gun_rotation=unreal.Rotator(pitch=0,yaw=-math.degrees(math.atan2(muzzle.y-grip.y,muzzle.x-grip.x)),roll=0).quaternion()
    def calibrated_socket(mesh,sequence):
        pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence,0,unreal.AnimPoseEvaluationOptions())
        hand=unreal.AnimPoseExtensions.get_bone_pose(pose,'RightHand',unreal.AnimPoseSpaces.WORLD)
        actor_hand=unreal.Rotator(pitch=0,yaw=-90,roll=0).quaternion()*hand.rotation
        relative=actor_hand.inversed()*gun_rotation
        offset=unreal.Transform(rotation=relative.rotator(),location=relative.rotate_vector(-grip))
        unreal.StingerAssetLibrary.add_mesh_socket(mesh,'RightHandSocket','RightHand',offset)
        print('CALIBRATED_SOCKET',mesh.get_name(),offset,'MUZZLE',muzzle,'GRIP',grip)
    calibrated_socket(MESH,unreal.load_asset(ROOT+'/Animations/ThirdPerson/Idle_Rifle_Hip_Retargeted_Stinger'))
    calibrated_socket(fp_mesh,find_animation('AR_M_1P_Idle01'))
    LIB.save_loaded_asset(fp_mesh)
    for socket,bone in [('FlagSocket','LeftHand'),('BackpackSocket','Spine2')]:
        # 持枪插槽以骨骼参考轴为基准；最终视觉偏移由角色设置覆盖。
        unreal.StingerAssetLibrary.add_mesh_socket(MESH,socket,bone,unreal.Transform())
    LIB.save_loaded_asset(MESH)
    fp_factory = unreal.AnimBlueprintFactory()
    fp_factory.set_editor_property('target_skeleton', SKELETON)
    fp_factory.set_editor_property('parent_class', unreal.StingerFirstPersonAnimInstance)
    fp = create('ABP_StingerFirstPerson',ROOT+'/Animations/FirstPerson',unreal.AnimBlueprint,fp_factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(fp)
    fp_cdo = unreal.get_default_object(fp.generated_class())
    for prop,name in [('idle_animation','AR_M_1P_Idle01'),('aim_animation','AR_M_1P_Idle01'),
                      ('sprint_animation','AR_M_1P_RelaxIdle'),('jump_animation','AR_M_1P_Idle01')]:
        fp_cdo.set_editor_property(prop,find_animation(name))
    LIB.save_loaded_asset(fp)
    montages = {}
    for prop,name in [('first_person_fire_montage','AR_M_1P_Fire'),
                      ('first_person_reload_montage','AR_M_1P_ChangeClipFull'),
                      ('first_person_swap_montage','AR_M_1P_GetWeapon')]:
        montage = unreal.StingerAssetLibrary.create_visual_montage(find_animation(name),ROOT+'/Animations/FirstPerson/AM_'+name)
        LIB.save_loaded_asset(montage)
        montages[prop] = montage
    bp_path = ROOT+'/Blueprints/BP_StingerCharacter'
    if not LIB.does_asset_exist(bp_path):
        LIB.duplicate_asset('/Game/Blueprints/Character/BP_BrokenEdgeCharacter',bp_path)
    bp = unreal.load_asset(bp_path)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    mesh = cdo.get_editor_property('mesh')
    mesh.set_skeletal_mesh_asset(MESH)
    mesh.set_anim_instance_class(LIB.load_blueprint_class(ROOT+'/Animations/ThirdPerson/ABP_BrokenEdgeAnimInstance_Stinger'))
    mesh.set_editor_property('relative_location',unreal.Vector(0,0,-92))
    mesh.set_editor_property('relative_rotation',unreal.Rotator(pitch=0,yaw=-90,roll=0))
    cdo.get_editor_property('capsule_component').set_capsule_size(34,92)
    for prop,value in {'first_person_mesh_asset':fp_mesh,'first_person_anim_class':fp.generated_class(),
                       'enable_sprint':True,'right_hand_bone':'RightHand',
                       'first_person_head_bone':'Head','sprint_speed':850.0,
                       'first_person_aim_fov':65.0,'first_person_aim_interp_speed':20.0,**montages}.items():
        cdo.set_editor_property(prop,value)
    cdo.set_editor_property('first_person_mesh_transform',unreal.Transform(location=unreal.Vector(55,0,-177),rotation=unreal.Rotator(pitch=0,yaw=-90,roll=0)))
    move = cdo.get_editor_property('character_movement')
    move.set_editor_property('max_walk_speed_crouched',300)
    move.set_editor_property('max_acceleration',2048)
    cdo.get_editor_property('follow_camera').set_editor_property('field_of_view',90)
    sprint = create('IA_StingerSprint',ROOT+'/Blueprints/Input',unreal.InputAction,unreal.InputAction_Factory())
    sprint.set_editor_property('value_type',unreal.InputActionValueType.BOOLEAN)
    mapping_path = ROOT+'/Blueprints/Input/IMC_Stinger'
    if not LIB.does_asset_exist(mapping_path): LIB.duplicate_asset('/Game/Input/IMC_CharacterDefault',mapping_path)
    mapping = unreal.load_asset(mapping_path)
    mapping.unmap_all_keys_from_action(sprint)
    shift = unreal.Key()
    shift.set_editor_property('key_name', 'LeftShift')
    mapping.map_key(sprint, shift)
    cdo.set_editor_property('default_mapping_context',mapping)
    cdo.set_editor_property('sprint_action',sprint)
    for prop,name in [('reload_montage','AM_Reload'),('fire_weapon_montage','AM_FireWeapon'),
                      ('swap_montage','AM_Swap'),('hit_react_montage','AM_HitReact'),('defeated_montage','AM_Defeated')]:
        cdo.set_editor_property(prop,unreal.load_asset(ROOT+'/Animations/ThirdPerson/'+name+'_Stinger'))
    for box in cdo.get_components_by_class(unreal.BoxComponent):
        key = box.get_name()
        if key in BONES:
            unreal.StingerAssetLibrary.reattach_component(box, mesh, BONES[key])
            box.set_editor_property('relative_location',unreal.Vector())
            box.set_box_extent(unreal.Vector(8,8,12) if key=='head' else unreal.Vector(8,8,15))
        if key in ['backpack','blanket']: box.set_box_extent(unreal.Vector())
    LIB.save_loaded_asset(sprint)
    LIB.save_loaded_asset(mapping)
    LIB.save_loaded_asset(bp)
    gm_path = ROOT+'/Blueprints/BP_StingerTestGameMode'
    if not LIB.does_asset_exist(gm_path): LIB.duplicate_asset('/Game/Blueprints/GameMode/BP_BrokenEdgeGameMode',gm_path)
    gm = unreal.load_asset(gm_path)
    unreal.BlueprintEditorLibrary.compile_blueprint(gm)
    gm_cdo = unreal.get_default_object(gm.generated_class())
    gm_cdo.set_editor_property('default_pawn_class',bp.generated_class())
    gm_cdo.set_editor_property('warmup_time',1.0)
    gm_cdo.set_editor_property('match_time',600.0)
    LIB.save_loaded_asset(gm)
    print('CHARACTER',bp_path)


def level():
    path = ROOT+'/Maps/L_StingerTest'
    complete = Path(unreal.Paths.project_saved_dir())/'Stinger'/'level_complete.json'
    if LIB.does_asset_exist(path) and complete.exists(): return
    editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not editor.new_level(path): raise RuntimeError('Cannot create test level')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.EditorLevelLibrary.get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode',LIB.load_blueprint_class(ROOT+'/Blueprints/BP_StingerTestGameMode'))
    def cube(label,position,scale):
        actor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*position))
        actor.set_actor_label(label)
        actor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
        actor.set_actor_scale3d(unreal.Vector(*scale))
        return actor
    cube('Stinger_TestFloor',(0,0,-25),(100,100,.5))
    for i in range(4): cube('Cover_'+str(i),(500+i*250,1000 if i%2 else -1000,70),(1.5,2,1.4))
    for location in [(-500,0,100),(-500,300,100)]: actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(*location))
    sun=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,600),unreal.Rotator(pitch=-45,yaw=-35,roll=0))
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property('intensity',5)
    actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,500))
    actors.spawn_actor_from_class(unreal.SkyAtmosphere,unreal.Vector())
    for i in range(2):
        actors.spawn_actor_from_class(LIB.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47'),unreal.Vector(-350,100+i*200,80))
        ammo = actors.spawn_actor_from_class(LIB.load_blueprint_class('/Game/Blueprints/Pickups/AmmoPickups/BP_SMG_Ammo'),unreal.Vector(0,100+i*200,40))
        ammo.set_editor_property('weapon_type', unreal.WeaponType.EWT_ASSAULT_RIFLE)
        ammo.set_editor_property('ammo_amount',90)
    display=actors.spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector(100,700,0),unreal.Rotator(pitch=0,yaw=90,roll=0))
    display.set_actor_label('Stinger_ThirdPerson_Inspection')
    display.skeletal_mesh_component.set_skeletal_mesh_asset(MESH)
    for location,text in [((-650,-300,180),'STINGER TEST | WASD MOVE | SHIFT SPRINT | SPACE JUMP'),
                          ((-650,-300,130),'E EQUIP | RMB AIM | LMB FIRE | R RELOAD | CTRL CROUCH | V VIEW')]:
        actor=actors.spawn_actor_from_class(unreal.TextRenderActor,unreal.Vector(*location))
        actor.text_render.set_text(text)
        actor.text_render.set_world_size(20)
    editor.save_current_level()
    complete.write_text(json.dumps({'map':path,'actors':len(actors.get_all_level_actors())}),encoding='utf-8')
    print('LEVEL',path)


stage = os.environ.get('STINGER_SETUP_STAGE','all')
if stage in ['all','retarget']: retarget()
if stage in ['all','configure']: configure()
if stage in ['all','level']: level()
