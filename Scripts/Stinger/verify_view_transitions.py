"""冷启动校验第一人称跳跃/开镜过渡绑定：骨架一致、时长有效、无游戏逻辑通知、旧槽位未被改动。

用法（编辑器关闭）：
  UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript ^
      -script="<绝对路径>/Scripts/Stinger/verify_view_transitions.py" -NullRHI -unattended -nosound
证据写入 Saved/Stinger/AnimationOptimization/VerifyViewTransitions.json。
"""
import json
import math
from pathlib import Path
import unreal

SET_PATH = '/Game/Characters/Stinger/Animations/DeltaForceLocomotion/DA_RifleLocomotion.DA_RifleLocomotion'
PRESENTATION_PATH = ('/Game/Characters/Stinger/Animations/DeltaForceLocomotion/'
                     'DA_AKLocomotionPresentation.DA_AKLocomotionPresentation')
NEW_SLOTS = ('ViewAimOn', 'ViewAimOff', 'ViewJumpStart', 'ViewJumpLoop', 'ViewJumpLand')
LOCOMOTION_SLOTS = ('ViewIdle', 'ViewWalk', 'ViewRun', 'ViewSprint',
                    'WalkToRun', 'RunToWalk', 'WalkToSprint', 'RunToSprint', 'SprintToWalk')
checks = []


def check(name, passed, detail=None):
    checks.append({'name': name, 'passed': bool(passed), 'detail': detail})


def main():
    movement = unreal.load_asset(SET_PATH)
    check('set_loads', movement is not None, SET_PATH)
    assert movement is not None
    reference = movement.get_editor_property('view_walk')
    check('reference_view_walk', reference is not None)
    skeleton = reference.get_editor_property('skeleton')

    for name in NEW_SLOTS:
        sequence = movement.get_editor_property(name)
        check(name + '_bound', sequence is not None)
        if sequence is None:
            continue
        check(name + '_same_skeleton',
              sequence.get_editor_property('skeleton') == skeleton,
              sequence.get_editor_property('skeleton').get_path_name())
        length = sequence.get_play_length()
        check(name + '_duration', math.isfinite(length) and length > 0.02, length)
        # 一次性表现不应携带结算通知，避免和第三人称结算重复。
        events = unreal.AnimationLibrary.get_animation_notify_events(sequence)
        check(name + '_no_notifies', len(events) == 0, len(events))

    for name in LOCOMOTION_SLOTS:
        check('legacy_' + name + '_intact', movement.get_editor_property(name) is not None)
    for name in ('CrouchSpeed', 'SprintSpeed', 'WalkSpeed', 'RunSpeed', 'RunThreshold'):
        check('legacy_' + name, movement.get_editor_property(name) > 0,
              movement.get_editor_property(name))

    presentation = unreal.load_asset(PRESENTATION_PATH)
    check('presentation_loads', presentation is not None)
    if presentation is not None:
        for name in ('Sprint', 'Jump'):
            sequence = presentation.get_editor_property(name)
            check('presentation_' + name, sequence is not None,
                  sequence.get_path_name() if sequence else None)
            if sequence is not None:
                check('presentation_' + name + '_same_skeleton',
                      sequence.get_editor_property('skeleton') == skeleton)

    passed = sum(1 for c in checks if c['passed'])
    evidence = {'passed': passed, 'total': len(checks), 'checks': checks}
    out = (Path(unreal.Paths.project_saved_dir()) / 'Stinger' / 'AnimationOptimization'
           / 'VerifyViewTransitions.json')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    failed = [c for c in checks if not c['passed']]
    unreal.log_warning('STINGER_VIEW_VERIFY %d/%d' % (passed, len(checks)))
    for c in failed:
        unreal.log_error('STINGER_VIEW_VERIFY_FAILED %s %s' % (c['name'], c['detail']))
    assert not failed, 'view transition verification failed: %s' % json.dumps(failed, ensure_ascii=False)


if __name__ == '__main__':
    main()
