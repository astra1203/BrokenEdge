"""Fit supporting fingers across the Stinger rifle locomotion poses."""
from pathlib import Path
import json
import shutil
import unreal

ROOT = '/Game/Characters/Stinger/Animations/ThirdPerson/'
SOURCE = '/Game/Characters/Stinger/Animations/Corrected/MappedV4/a72d8004/AR_M_1P_Idle01'
FINGER_BONES = ['LeftHand' + finger + str(i) for finger in ['Index', 'Middle', 'Ring', 'Pinky', 'Thumb'] for i in [1, 2, 3]] + ['LeftInHandPinky', 'LeftInHandRing']
TARGET_NAMES = [prefix + 'Rifle_' + stance + '_Retargeted_Stinger'
                for prefix in ['Idle_', 'Crouch_Idle_'] for stance in ['Hip', 'Ironsights']]
TARGET_NAMES += ['Crouch_Walk_' + direction + '_Rifle_' + stance + '_Retargeted_Stinger'
                 for direction in ['Fwd', 'Bwd', 'Lt', 'Rt'] for stance in ['Hip', 'Ironsights']]
TARGET_NAMES += ['Walk_' + direction + '_Rifle_Ironsights_Retargeted_Stinger' for direction in ['Fwd', 'Bwd', 'Lt', 'Rt']]
TARGET_NAMES += ['Jog_' + direction + '_Rifle_Retargeted_Stinger' for direction in ['Fwd', 'Bwd', 'Lt', 'Rt']]

def calibrate():
    source = unreal.load_asset(SOURCE)
    assert source
    bones = FINGER_BONES
    rotations = {b: unreal.AnimationLibrary.get_bone_pose_for_time(source, b, 0, False).rotation for b in bones}
    targets = [unreal.load_asset(ROOT + name) for name in TARGET_NAMES]
    assert all(isinstance(asset, unreal.AnimSequence) for asset in targets)
    backup = Path(unreal.Paths.project_saved_dir()) / 'Stinger' / 'SupportHandBackup'
    backup.mkdir(parents=True, exist_ok=True)
    written = []
    for sequence in targets:
        disk = Path(unreal.Paths.project_content_dir()) / (sequence.get_path_name().split('.')[0][6:] + '.uasset')
        destination = backup / disk.name
        if disk.exists() and not destination.exists():
            shutil.copy2(disk, destination)
        frames = unreal.AnimationLibrary.get_num_frames(sequence)
        poses = {b: [unreal.AnimationLibrary.get_bone_pose_for_frame(sequence, b, i, False) for i in range(frames + 1)] for b in bones}
        controller = sequence.get_editor_property('controller')
        controller.open_bracket('Fit Stinger supporting fingers')
        try:
            for bone in bones:
                assert controller.set_bone_track_keys(bone, [t.translation for t in poses[bone]], [rotations[bone]] * (frames + 1), [t.scale3d for t in poses[bone]])
        finally:
            controller.close_bracket()
        unreal.StingerAssetLibrary.finalize_animation(sequence)
        assert unreal.EditorAssetLibrary.save_loaded_asset(sequence)
        written.append(sequence.get_path_name())
    report = {'source': SOURCE, 'bones': bones, 'assets': written, 'scope': 'supporting finger rotations in 20 rifle idle/crouch/walk/jog clips; wrist/socket fit is configured separately; action montages and unarmed clips are unchanged'}
    (backup.parent / 'support_hand_calibration.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('SUPPORT_HAND_CALIBRATED', len(written))
    return report

if __name__ == '__main__':
    calibrate()
