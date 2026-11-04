"""34 号：栓狙/霰弹枪原来借用 AK 的表现配置，第一人称显示的是 AK 枪模。
给它们各做一份表现配置：第一人称用自己的枪模，握把/护木插槽从 AK 的插槽换算过来
（AK 枪口朝 +Y，MilitaryWeapSilver 枪口朝 +X、握把在原点），瞄准高度按瞄具高度下调。"""
import unreal
lib=unreal.EditorAssetLibrary
L=lambda s: unreal.log('LONGGUN '+str(s))
AK_DA='/Game/Characters/Stinger/Animations/DeltaForceLocomotion/DA_AKLocomotionPresentation'
AK_MESH='/Game/Characters/Stinger/Meshes/FirstPerson/ViewFit/SK_KA47_ViewFit'
ak=unreal.load_asset(AK_MESH)
akpose=unreal.AnimPoseExtensions.get_reference_pose(ak.get_editor_property('skeleton'))
def bone_cs(pose,bone): return unreal.AnimPoseExtensions.get_ref_bone_pose(pose,bone,unreal.AnimPoseSpaces.WORLD)
def socket_cs(mesh,pose,name):
    s=mesh.find_socket(name)
    local=unreal.Transform(s.relative_location,s.relative_rotation,s.relative_scale)
    return local.multiply(bone_cs(pose,s.bone_name))
ak_right=socket_cs(ak,akpose,'Hand_R'); ak_left=socket_cs(ak,akpose,'LeftHandSocket')
L('ak right %s left %s'%(ak_right.translation,ak_left.translation))
# 本枪组件空间 -> AK 组件空间：绕 Z 转 +90（+X 变 +Y），握把中心对到 AK 握把中心 (0,-1,0)，再整体抬 dz。
def to_ak(dz): return unreal.Transform(unreal.Vector(0,-1,dz),unreal.Rotator(yaw=90),unreal.Vector(1,1,1))
import os
ONLY=os.environ.get('LONGGUN_ONLY')
# 结构体属性在 Python 里是引用，改它会改到内存里的 AK 配置；所以一次进程只配一把枪。
for name,src,sight,mag_bone in [('BoltSniper','/Game/MilitaryWeapSilver/Weapons/Sniper_Rifle_A',21.5,'Ammo'),
                                ('PumpShotgun','/Game/MilitaryWeapSilver/Weapons/Shotgun_A',14.5,'Ammo')]:
    if ONLY and ONLY!=name: continue
    folder='/Game/Extraction/Weapons/'+name
    dest=folder+'/SK_%s_ViewFit'%name
    if not lib.does_asset_exist(dest): assert lib.duplicate_asset(src,dest)
    mesh=unreal.load_asset(dest)
    pose=unreal.AnimPoseExtensions.get_reference_pose(mesh.get_editor_property('skeleton'))
    root=mesh.get_editor_property('skeleton').get_editor_property('bone_tree') if False else None
    grip_bone='Grip_Bone'
    grip=bone_cs(pose,grip_bone)
    M=to_ak(0); Minv=M.inverse()
    for sock,akt in [('Hand_R',ak_right),('LeftHandSocket',ak_left)]:
        cs=akt.multiply(Minv)
        offset=cs.multiply(grip.inverse())
        if mesh.find_socket(sock):
            unreal.StingerAssetLibrary.remove_mesh_socket(mesh,sock) if hasattr(unreal.StingerAssetLibrary,'remove_mesh_socket') else None
        unreal.StingerAssetLibrary.add_mesh_socket(mesh,sock,grip_bone,offset)
        L('%s %s cs=%s'%(name,sock,cs.translation))
    assert lib.save_loaded_asset(mesh,only_if_is_dirty=False)
    da_path=folder+'/DA_%s_Presentation'%name
    if not lib.does_asset_exist(da_path): assert lib.duplicate_asset(AK_DA,da_path)
    da=unreal.load_asset(da_path)
    da.set_editor_property('view_mesh',mesh)
    fit=unreal.load_asset(AK_DA).get_editor_property('view_fit')
    aim=fit.get_editor_property('aim')
    aim.translation=unreal.Vector(aim.translation.x,aim.translation.y,aim.translation.z-(sight-13.95))
    fit.set_editor_property('aim',aim)
    fit.set_editor_property('magazine_bone',mag_bone)
    for prop in ['magazine_grip','magazine_reference']:
        fit.set_editor_property(prop,fit.get_editor_property(prop).multiply(Minv))
    fit.set_editor_property('magazine_reference',bone_cs(pose,mag_bone))
    da.set_editor_property('view_fit',fit)
    assert lib.save_loaded_asset(da,only_if_is_dirty=False)
    bp=unreal.load_asset(folder+'/BP_'+name)
    cdo=unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property('presentation',da)
    assert lib.save_loaded_asset(bp,only_if_is_dirty=False)
    L('%s done aimz=%.2f'%(name,aim.translation.z))
