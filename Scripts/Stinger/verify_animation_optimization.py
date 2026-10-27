"""只读校验本轮动画优化：第一人称跳跃/开镜、技能蒙太奇、翻越动画集、第三人称瞄准偏航参数。

用法（可与其他只读命令并行，编辑器开着也能跑）：
  UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript ^
      -script="<绝对路径>/Scripts/Stinger/verify_animation_optimization.py" -NullRHI -unattended -nosound
证据写入 Saved/Stinger/AnimationOptimization/VerifyAll.json。
"""
import json
import math
from pathlib import Path
import unreal

LIB = unreal.EditorAssetLibrary
SET_PATH = '/Game/Characters/Stinger/Animations/DeltaForceLocomotion/DA_RifleLocomotion.DA_RifleLocomotion'
TRAVERSAL_PATH = '/Game/Extraction/Traversal/DA_TraversalAnimations.DA_TraversalAnimations'
BLUEPRINTS = ('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter',
              '/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired')
ABILITY_SLOTS = ('HealMontage', 'FirstPersonHealMontage', 'ReviveMontage',
                 'FirstPersonReviveMontage', 'SmokeMontage', 'FirstPersonSmokeMontage')
VIEW_SLOTS = ('ViewAimOn', 'ViewAimOff', 'ViewJumpStart', 'ViewJumpLoop', 'ViewJumpLand')
checks = []


def check(name, passed, detail=None):
    checks.append({'name': name, 'passed': bool(passed), 'detail': detail})


def main():
    movement = unreal.load_asset(SET_PATH)
    check('rifle_set_loads', movement is not None, SET_PATH)
    assert movement is not None
    skeleton = movement.get_editor_property('view_walk').get_editor_property('skeleton')
    for name in VIEW_SLOTS:
        sequence = movement.get_editor_property(name)
        check('view_' + name, sequence is not None)
        if sequence is None:
            continue
        check('view_' + name + '_skeleton', sequence.get_editor_property('skeleton') == skeleton)
        check('view_' + name + '_duration', sequence.get_play_length() > .02)
        check('view_' + name + '_no_notifies',
              len(unreal.AnimationLibrary.get_animation_notify_events(sequence)) == 0)
    check('aim_yaw_scale', 0 < movement.get_editor_property('AimYawScale') <= 1,
          movement.get_editor_property('AimYawScale'))
    check('aim_yaw_clamp', movement.get_editor_property('AimYawClamp') > 10,
          movement.get_editor_property('AimYawClamp'))

    for blueprint in BLUEPRINTS:
        bp = LIB.load_asset(blueprint)
        check('bp_loads_' + blueprint.rsplit('/', 1)[-1], bp is not None)
        if bp is None:
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        for prop in ABILITY_SLOTS:
            montage = cdo.get_editor_property(prop)
            check('bp_%s.%s' % (blueprint.rsplit('/', 1)[-1], prop), montage is not None)
            if montage is None:
                continue
            length = montage.get_play_length()
            check('bp_%s.%s_duration' % (blueprint.rsplit('/', 1)[-1], prop),
                  math.isfinite(length) and length > .1, length)

    traversal = unreal.load_asset(TRAVERSAL_PATH)
    check('traversal_set_loads', traversal is not None, TRAVERSAL_PATH)
    if traversal is not None:
        entries = traversal.get_editor_property('Animations')
        check('traversal_entries', len(entries) >= 10, len(entries))
        missing = []
        kinds = set()
        for entry in entries:
            kind = entry.get_editor_property('kind')
            kinds.add(str(kind))
            for role in ('firstPerson', 'thirdPerson'):
                sequence = entry.get_editor_property(role)
                if sequence is None:
                    missing.append((str(kind), entry.get_editor_property('height'), role))
        check('traversal_all_bound', not missing, missing)
        check('traversal_vault_and_mantle',
              any('VAULT' in k for k in kinds) and any('MANTLE' in k for k in kinds), sorted(kinds))
        check('traversal_ladder', any('LADDER' in k for k in kinds))

    passed = sum(1 for c in checks if c['passed'])
    evidence = {'passed': passed, 'total': len(checks), 'checks': checks}
    out = (Path(unreal.Paths.project_saved_dir()) / 'Stinger' / 'AnimationOptimization' / 'VerifyAll.json')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    failed = [c for c in checks if not c['passed']]
    unreal.log_warning('STINGER_VERIFY_ALL %d/%d' % (passed, len(checks)))
    for c in failed:
        unreal.log_error('STINGER_VERIFY_ALL_FAILED %s %s' % (c['name'], c['detail']))
    assert not failed, 'verification failed: %s' % json.dumps(failed, ensure_ascii=False)


if __name__ == '__main__':
    main()
