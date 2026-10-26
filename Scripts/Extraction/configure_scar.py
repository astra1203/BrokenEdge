"""Configure a playable basic SCAR candidate. Numeric balance and fit are provisional."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
lib = unreal.EditorAssetLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()
folder = '/Game/Extraction/Weapons/SCARH'
report = json.loads((root/'Saved/Extraction/SpaceCenterExpansion01/ScarAnimations.json').read_text(encoding='utf-8'))

def sequence(suffix):
    entry = report['AR_M_1P_Scar-H_'+suffix]['sequences'][0]
    assert entry['validation'] == 'numeric_passed'
    return unreal.load_asset(entry['assets']['Mapped'])

def duplicate(source, target):
    return unreal.load_asset(target) if lib.does_asset_exist(target) else lib.duplicate_asset(source, target)

idle = sequence('idle')
pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(idle,0,unreal.AnimPoseEvaluationOptions())
right = unreal.AnimPoseExtensions.get_bone_pose(pose,'RightHand',unreal.AnimPoseSpaces.WORLD)
left = unreal.AnimPoseExtensions.get_bone_pose(pose,'LeftHand',unreal.AnimPoseSpaces.WORLD)
sockets = json.loads((root/'Saved/Extraction/SpaceCenterExpansionSources01/AssemblySockets.json').read_text(encoding='utf-8'))

def position(model, view, socket):
    row = sockets[model+'_'+view]
    p = row['SocketLocation'][row['SocketNames'].index(socket)]
    return unreal.Vector(p['X'],p['Y'],p['Z'])

meshes = {}
for view in ('1P','3P'):
    mesh = unreal.load_asset(folder+'/SocketAssembly'+view+'/SCARH_Candidate_'+view+'/SkeletalMeshes/SCARH_Candidate_'+view)
    assert mesh
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    bone = comp.get_socket_transform('Root',unreal.RelativeTransformSpace.RTS_COMPONENT)
    receiver = 'Rec_AR_ScarH_045'
    barrel = position(receiver,view,'Bar_point')
    muzzle = barrel+position('Bar_M-400_scarH_055',view,'MuzzleFlashSocket')
    support = position(receiver,view,'Han_point')+position('Han_ScarH_std_066',view,'HandIKPoint')
    grip = position(receiver,view,'PisG_point')+unreal.Vector(0,-2,-3)
    for name,point,rotation in [('MuzzleFlash',muzzle,unreal.Rotator(yaw=90).quaternion()),
                               ('Hand_R',grip,right.rotation),('LeftHandSocket',support,left.rotation)]:
        offset = unreal.Transform(location=bone.rotation.inversed().rotate_vector(point-bone.translation),
                                  rotation=(bone.rotation.inversed()*rotation).rotator())
        unreal.StingerAssetLibrary.add_mesh_socket(mesh,name,'Root',offset)
    assert lib.save_loaded_asset(mesh)
    meshes[view] = mesh

profile_class = unreal.load_class(None,'/Script/BrokenEdge.WeaponPresentation')
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class',profile_class)
profile = unreal.load_asset(folder+'/DA_SCARH_Presentation')
if not profile: profile = assets.create_asset('DA_SCARH_Presentation',folder,profile_class,factory)
fit = unreal.StingerViewWeaponFit()
fit.set_editor_property('enabled',True)
fit.set_editor_property('hip',unreal.Transform(location=unreal.Vector(35,12,-23),rotation=unreal.Rotator(yaw=-90)))
fit.set_editor_property('aim',unreal.Transform(location=unreal.Vector(20,0,-16.7),rotation=unreal.Rotator(yaw=-90)))
fit.set_editor_property('source_right_hand',right)
fit.set_editor_property('source_left_hand',left)
fit.set_editor_property('magazine_bone','Mag_point')
comp.set_skeletal_mesh_asset(meshes['1P'])
fit.set_editor_property('magazine_reference',comp.get_socket_transform('Mag_point',unreal.RelativeTransformSpace.RTS_COMPONENT))
reload = sequence('ChangclipFull')
for prop,fraction in [('source_left_magazine_out',.12),('source_left_magazine_in',.52)]:
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(reload,reload.get_play_length()*fraction,unreal.AnimPoseEvaluationOptions())
    fit.set_editor_property(prop,unreal.AnimPoseExtensions.get_bone_pose(pose,'LeftHand',unreal.AnimPoseSpaces.WORLD))
fit.set_editor_property('magazine_grip',unreal.Transform(location=fit.magazine_reference.translation+unreal.Vector(-3,0,-8),
                                    rotation=fit.source_left_magazine_out.rotation.rotator()))
profile.set_editor_property('view_fit',fit)
profile.set_editor_property('view_mesh',meshes['1P'])
profile.set_editor_property('idle',idle)
profile.set_editor_property('aim',sequence('AimIdle'))
for prop,suffix in [('view_fire','Fire'),('view_reload','ChangclipFull'),('view_swap','GetWeapon')]:
    montage = unreal.StingerAssetLibrary.create_visual_montage(sequence(suffix),folder+'/Montages/AM_'+suffix)
    assert montage and lib.save_loaded_asset(montage)
    profile.set_editor_property(prop,montage)
assert lib.save_loaded_asset(profile)
bp = duplicate('/Game/Blueprints/Weapon/BP_AK47',folder+'/BP_SCARH')
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cdo = unreal.get_default_object(bp.generated_class())
cdo.set_editor_property('presentation',profile)
component = cdo.get_editor_property('weapon_mesh')
component.set_skeletal_mesh_asset(meshes['3P'])
component.set_editor_property('override_materials',[])
for prop,value in [('ammo',20),('mag_capacity',20),('fire_delay',.12),('damage',35.),('head_shot_damage',70.),('fire_animation',None)]:
    cdo.set_editor_property(prop,value)
assert lib.save_loaded_asset(bp,only_if_is_dirty=False)
gun = duplicate('/Game/Extraction/Items/DA_Item_AK','/Game/Extraction/Items/DA_Item_SCARH')
for prop,value in {'item_id':'SCARH','label':'SCAR-H','weapon_class':bp.generated_class(),
                   'magazine_capacity':20,'default_ammo':'Ammo76251','compatible_ammo':['Ammo76251'],
                   'compatible_attachments':['RedDot','VerticalGrip','Compensator'], 'description':'7.62×51 mm，20 发弹匣。基础组装；射速、伤害与握持仍为临时调校。'}.items():
    gun.set_editor_property(prop,value)
assert lib.save_loaded_asset(gun)
ammo = duplicate('/Game/Extraction/Items/DA_Item_Ammo','/Game/Extraction/Items/DA_Item_Ammo76251')
for prop,value in {'item_id':'Ammo76251','label':'7.62×51 mm 弹药','description':'SCAR-H 适用；临时基础弹种。'}.items():
    ammo.set_editor_property(prop,value)
assert lib.save_loaded_asset(ammo)
print('SCAR_CONFIGURED',bp.get_path_name(),profile.get_path_name())
