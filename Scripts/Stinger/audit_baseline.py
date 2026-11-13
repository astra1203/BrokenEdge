"""Read the saved Stinger baseline assets in a fresh process; never save them."""
import sys as _presentation_sys
from pathlib import Path as _PresentationPath
_presentation_sys.path.insert(0, str(_PresentationPath(__file__).resolve().parents[1] / "Stinger"))
from character_presentation import get_character_property
import json
import math
import os
import runpy
from pathlib import Path
import traceback
import unreal

out = Path(os.environ['STINGER_BASELINE_DIR'])
report = {'checks': [], 'characters': []}
lib = unreal.EditorAssetLibrary

def check(name, ok, detail=None):
    report['checks'].append({'name': name, 'passed': bool(ok), 'detail': detail})

def angle(a, b):
    dot = sum(getattr(a, axis) * getattr(b, axis) for axis in ('x', 'y', 'z', 'w'))
    return math.degrees(2 * math.acos(min(1, abs(dot))))

try:
    root = '/Game/Characters/Stinger'
    gm = unreal.get_default_object(lib.load_blueprint_class(root + '/Blueprints/BP_StingerTestGameMode'))
    report['game_mode'] = gm.get_class().get_path_name()
    pawn_class = gm.get_editor_property('default_pawn_class')
    report['default_pawn'] = pawn_class.get_path_name()
    check('game_mode_uses_promoted_character', pawn_class.get_name() == 'BP_StingerCharacter_C', report['default_pawn'])
    for name in ['BP_StingerCharacter', 'BP_StingerCharacter_Repaired']:
        bp = unreal.load_asset(root + '/Blueprints/' + name)
        cdo = unreal.get_default_object(bp.generated_class())
        body = get_character_property(cdo, 'mesh')
        row = {'name': name, 'parent': unreal.BlueprintEditorLibrary.get_blueprint_parent_class(bp).get_path_name(),
               'spawn_health': get_character_property(cdo, 'health'), 'max_health': get_character_property(cdo, 'max_health'),
               'body': body.get_skeletal_mesh_asset().get_path_name(), 'anim_class': body.get_editor_property('anim_class').get_path_name(),
               'first_person_mesh': get_character_property(cdo, 'first_person_mesh_asset').get_path_name(), 'montages': {}}
        check(name + '_cpp_parent', row['parent'] == '/Script/BrokenEdge.BrokenEdgeCharacter', row['parent'])
        for field in ['fire_weapon_montage', 'reload_montage', 'swap_montage', 'defeated_montage']:
            value = get_character_property(cdo, field)
            row['montages'][field] = value.get_path_name() if value else None
            check(name + '_' + field, value is not None, row['montages'][field])
        report['characters'].append(row)
    animation = unreal.load_asset(root + '/Animations/ThirdPerson/ABP_BrokenEdgeAnimInstance_Stinger')
    report['ik'] = json.loads(unreal.StingerAssetLibrary.describe_third_person_grip_ik(animation))
    check('left_wrist_copies_target_rotation', len(report['ik']['fabrik']) == 1 and report['ik']['fabrik'][0]['rotation_source'] == 2, report['ik'])
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    weapon = actors.spawn_actor_from_class(lib.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47'), unreal.Vector())
    try:
        mesh = weapon.get_editor_property('weapon_mesh')
        world_grip = mesh.get_socket_transform('LeftHandSocket', unreal.RelativeTransformSpace.RTS_COMPONENT)
        mesh.set_skeletal_mesh_asset(unreal.load_asset(root + '/Meshes/FirstPerson/ViewFit/SK_KA47_ViewFit'))
        view_grip = mesh.get_socket_transform('LeftHandSocket', unreal.RelativeTransformSpace.RTS_COMPONENT)
        check('world_grip_saved_palm_contact_anchor', (world_grip.translation - unreal.Vector(4, 25, 6)).length() < .01,
              {'world': str(world_grip.translation), 'view': str(view_grip.translation)})
        check('world_grip_uses_component_space_rotation', angle(world_grip.rotation, view_grip.rotation) < 1,
              angle(world_grip.rotation, view_grip.rotation))
    finally:
        actors.destroy_actor(weapon)
    source = unreal.load_asset(root + '/Animations/Corrected/MappedV4/a72d8004/AR_M_1P_Idle01')
    finger_config = runpy.run_path(str(Path(__file__).with_name('calibrate_support_hand.py')))
    fingers = finger_config['FINGER_BONES']
    for name in finger_config['TARGET_NAMES']:
        sequence = unreal.load_asset(root + '/Animations/ThirdPerson/' + name)
        errors = {bone: angle(unreal.AnimationLibrary.get_bone_pose_for_time(source, bone, 0, False).rotation,
                              unreal.AnimationLibrary.get_bone_pose_for_time(sequence, bone, 0, False).rotation) for bone in fingers}
        for sample_time in [sequence.get_play_length() / 2, sequence.get_play_length()]:
            for bone in fingers:
                errors[bone] = max(errors[bone], angle(unreal.AnimationLibrary.get_bone_pose_for_time(source, bone, 0, False).rotation,
                                                       unreal.AnimationLibrary.get_bone_pose_for_time(sequence, bone, sample_time, False).rotation))
        check(name + '_saved_finger_pose', max(errors.values()) < 1, errors)
except Exception:
    report['error'] = traceback.format_exc()
report['result'] = 'passed' if report['checks'] and all(c['passed'] for c in report['checks']) and 'error' not in report else 'failed'
(out / 'Audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('STINGER_BASELINE_AUDIT', json.dumps(report))
