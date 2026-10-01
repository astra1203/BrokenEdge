import unreal, json
from pathlib import Path
report = {}
report['skeleton_classes'] = [x for x in dir(unreal) if 'Skeleton' in x or 'AnimPose' in x]
report['mesh_methods'] = [x for x in dir(unreal.SkeletalMesh) if 'bone' in x or 'pose' in x or 'skeleton' in x]
report['mesh_subsystem'] = [x for x in dir(unreal.SkeletalMeshEditorSubsystem) if 'bone' in x or 'pose' in x or 'socket' in x]
report['pose_methods'] = [x for x in dir(unreal.AnimPoseExtensions) if 'pose' in x or 'bone' in x]
report['rig_methods'] = [x for x in dir(unreal.IKRigController) if 'bone' in x or 'chain' in x or 'retarget' in x]
report['retarget_methods'] = [x for x in dir(unreal.IKRetargeterController) if 'rig' in x or 'pose' in x or 'chain' in x]
report['docs'] = {}
for cls, method in [(unreal.IKRigController, 'set_skeletal_mesh'),(unreal.IKRigController,'add_retarget_chain'),
                    (unreal.IKRetargeterController,'set_ik_rig'),(unreal.IKRetargeterController,'auto_map_chains'),
                    (unreal.IKRetargeterController,'auto_align_all_bones'),(unreal.AnimPoseExtensions,'get_reference_pose'),
                    (unreal.EditorLevelLibrary,'new_level'),(unreal.EditorLevelLibrary,'save_current_level')]:
    report['docs'][cls.__name__+'.'+method]=getattr(cls,method).__doc__
base = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class('/Game/Blueprints/Character/BP_BrokenEdgeCharacter'))
report['base_character'] = {name:str(base.get_editor_property(name)) for name in ['default_mapping_context','jump_action','move_action','look_action','crouch_action','equip_action','aim_action','reload_action','drop_action','fire_action','toggle_camera_view_action','fire_weapon_montage','reload_montage','swap_montage','hit_react_montage','defeated_montage']}
bp = unreal.load_asset('/Game/Blueprints/Character/Animation/ABP_BrokenEdgeAnimInstance')
report['source_skeleton']=bp.get_editor_property('target_skeleton').get_path_name()
assets = unreal.EditorAssetLibrary.list_assets('/Game/Characters/Stinger/Meshes/Gameplay',recursive=True)
for path in assets:
    obj=unreal.load_asset(path)
    if isinstance(obj, unreal.SkeletalMesh):
        report['mesh']=path
        report['skeleton']=obj.get_editor_property('skeleton').get_path_name()
        component = unreal.SkeletalMeshComponent()
        component.set_skeletal_mesh_asset(obj)
        report['reference_pose'] = {str(component.get_bone_name(i)): str(component.get_ref_pose_transform(i)) for i in range(component.get_num_bones())}
Path(unreal.Paths.project_saved_dir(),'Stinger').mkdir(parents=True,exist_ok=True)
Path(unreal.Paths.project_saved_dir(),'Stinger','inspect.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
