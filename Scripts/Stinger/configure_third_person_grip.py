"""Calibrate mesh-only world AK anchors without changing first-person fitting assets."""
import json
from pathlib import Path
import unreal

lib = unreal.EditorAssetLibrary
helper = unreal.StingerAssetLibrary
root = '/Game/Characters/Stinger'
out = Path(unreal.Paths.project_saved_dir()) / 'Stinger'
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
weapon = actors.spawn_actor_from_class(lib.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47'), unreal.Vector())
component = weapon.get_editor_property('weapon_mesh')
mesh = component.get_skeletal_mesh_asset()
assert mesh.get_path_name() == '/Game/FPS_Weapon_Bundle/Weapons/Meshes/Ka47/SK_KA47.SK_KA47'
assert component.does_socket_exist('b_gun_Root')
root_bone = component.get_socket_transform('b_gun_Root', unreal.RelativeTransformSpace.RTS_COMPONENT)
report = {'weapon': mesh.get_path_name(), 'before_sockets': {n: component.does_socket_exist(n) for n in ['Hand_R', 'LeftHandSocket']}}
# Component-space anchors on the actual AK geometry, independent of character pose.
grips = {'Hand_R': unreal.Vector(0, -8, 0), 'LeftHandSocket': unreal.Vector(4, 20, 9)}
grip_pose = unreal.load_asset(root + '/Animations/Corrected/MappedV4/a72d8004/AR_M_1P_Idle01')
left_rotation = unreal.AnimationLibrary.get_bone_pose_for_time(grip_pose, 'LeftHand', 0, False).rotation
for name, location in grips.items():
    local = unreal.Transform(location=root_bone.rotation.inversed().rotate_vector(location - root_bone.translation),
                             rotation=(root_bone.rotation.inversed() * (left_rotation if name == 'LeftHandSocket' else unreal.Quat())).rotator())
    helper.add_mesh_socket(mesh, name, 'b_gun_Root', local)
assert lib.save_loaded_asset(mesh)
for name, location in grips.items():
    assert component.does_socket_exist(name)
    assert (component.get_socket_location(name) - location).length() < .01
actors.destroy_actor(weapon)

bodies = set()
report['characters'] = []
for name in ['BP_StingerCharacter', 'BP_StingerCharacter_Repaired']:
    character = unreal.get_default_object(lib.load_blueprint_class(root + '/Blueprints/' + name))
    body = character.get_editor_property('mesh')
    asset = body.get_skeletal_mesh_asset()
    assert str(character.get_editor_property('right_hand_bone')) == 'RightHand'
    assert body.does_socket_exist('RightHandSocket')
    original = body.get_socket_transform('RightHandSocket', unreal.RelativeTransformSpace.RTS_PARENT_BONE_SPACE)
    if asset.get_path_name() not in bodies:
        grip = grips['Hand_R']
        inverse_grip = unreal.Vector(-grip.x, -grip.y, -grip.z)
        offset = unreal.Transform(location=original.rotation.rotate_vector(inverse_grip),
                                  rotation=original.rotation.rotator(), scale=original.scale3d)
        helper.add_mesh_socket(asset, 'RightHandSocket', 'RightHand', offset)
        assert lib.save_loaded_asset(asset)
        bodies.add(asset.get_path_name())
    report['characters'].append({'character': name, 'body': asset.get_path_name(), 'right_socket_before': str(original),
                                 'right_socket_after': str(body.get_socket_transform('RightHandSocket', unreal.RelativeTransformSpace.RTS_PARENT_BONE_SPACE))})

animation = unreal.load_asset(root + '/Animations/ThirdPerson/ABP_BrokenEdgeAnimInstance_Stinger')
report['ik_before'] = json.loads(helper.describe_third_person_grip_ik(animation))
assert helper.configure_third_person_grip_ik(animation)
# Also correct existing assets when the loaded helper predates the rotation change.
fabrik = unreal.find_object(None, animation.get_path_name() + ':AnimGraph.AnimGraphNode_Fabrik_0')
assert fabrik
node = fabrik.get_editor_property('node')
node.set_editor_property('effector_rotation_source', unreal.BoneRotationSource.BRS_COPY_FROM_TARGET)
fabrik.set_editor_property('node', node)
unreal.BlueprintEditorLibrary.compile_blueprint(animation)
unreal.get_default_object(animation.generated_class()).set_editor_property('grip_reach_correction_weapon_mesh', mesh)
assert lib.save_loaded_asset(animation)
report['ik_after'] = json.loads(helper.describe_third_person_grip_ik(animation))
assert report['ik_after']['fabrik'][0]['root'] == 'LeftArm'
assert report['ik_after']['fabrik'][0]['tip'] == 'LeftHand'
assert report['ik_after']['fabrik'][0]['effector_bone'] == 'RightHand'
assert report['ik_after']['fabrik'][0]['rotation_source'] == 2
report['grip_component_positions'] = {n: [v.x, v.y, v.z] for n, v in grips.items()}
(out / 'third_person_grip_configuration.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('THIRD_PERSON_GRIP_CONFIGURED', json.dumps(report))
