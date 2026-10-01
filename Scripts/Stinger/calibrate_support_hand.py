"""Fit the supporting fingers in the two Stinger rifle idle poses."""
from pathlib import Path
import json
import shutil
import unreal

ROOT = '/Game/Characters/Stinger/Animations/ThirdPerson/'
SOURCE = '/Game/Characters/Stinger/Animations/Corrected/MappedV4/a72d8004/AR_M_1P_Idle01'

def calibrate():
    source = unreal.load_asset(SOURCE)
    assert source
    bones = ['LeftHand' + finger + str(i) for finger in ['Index', 'Middle', 'Ring', 'Pinky', 'Thumb'] for i in [1, 2, 3]]
    bones += ['LeftInHandPinky', 'LeftInHandRing']
    rotations = {b: unreal.AnimationLibrary.get_bone_pose_for_time(source, b, 0, False).rotation for b in bones}
    targets = [unreal.load_asset(ROOT + name) for name in ['Idle_Rifle_Hip_Retargeted_Stinger', 'Idle_Rifle_Ironsights_Retargeted_Stinger']]
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
    report = {'source': SOURCE, 'bones': bones, 'assets': written, 'scope': 'supporting finger rotations in two idle poses; wrist/socket fit is configured separately'}
    (backup.parent / 'support_hand_calibration.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('SUPPORT_HAND_CALIBRATED', len(written))
    return report

if __name__ == '__main__':
    calibrate()
