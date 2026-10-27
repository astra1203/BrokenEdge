"""把第一人称跳跃三段与开镜/关镜过渡绑定到 DA_RifleLocomotion，并修正 AK 表现资产的 Sprint 引用。

用法（编辑器必须关闭，命令行走独立进程）：
  UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript ^
      -script="<本项目绝对路径>/Scripts/Stinger/configure_view_transitions.py" -NullRHI -unattended -nosound

脚本幂等：已绑定相同资产时 report 中 changed 为空。证据写入
Saved/Stinger/AnimationOptimization/ConfigureViewTransitions.json。
"""
import json
import unreal
from pathlib import Path

LIB = unreal.EditorAssetLibrary
SET_PATH = '/Game/Characters/Stinger/Animations/DeltaForceLocomotion/DA_RifleLocomotion.DA_RifleLocomotion'
PRESENTATION_PATH = ('/Game/Characters/Stinger/Animations/DeltaForceLocomotion/'
                     'DA_AKLocomotionPresentation.DA_AKLocomotionPresentation')
MAPPED = '/Game/Characters/Stinger/Animations/Corrected/MappedV4'
LOCOMOTION = '/Game/Characters/Stinger/Animations/DeltaForceLocomotion/MappedV4'

# 跳跃只有开镜版被重映射到目标骨架；第一人称前臂由 IK 绑定到武器，
# 因此同一组序列在腰射与开镜下的差异只体现在躯干，可共用。
BINDINGS = {
    'ViewJumpStart': MAPPED + '/52f0dbef/AR_M_1P_AimJumpStart.AR_M_1P_AimJumpStart',
    'ViewJumpLoop': MAPPED + '/22d89289/AR_M_1P_AimJumpLoop.AR_M_1P_AimJumpLoop',
    'ViewJumpLand': MAPPED + '/5628d1b9/AR_M_1P_AimJumpLand.AR_M_1P_AimJumpLand',
    'ViewAimOn': MAPPED + '/7372680e/AR_M_1P_AimOn.AR_M_1P_AimOn',
    'ViewAimOff': MAPPED + '/0684eb34/AR_M_1P_AimOff.AR_M_1P_AimOff',
}
PRESENTATION_BINDINGS = {
    'Sprint': LOCOMOTION + '/8fb4ffca/AR_M_1P_Sprint_Loop.AR_M_1P_Sprint_Loop',
    'Jump': MAPPED + '/22d89289/AR_M_1P_AimJumpLoop.AR_M_1P_AimJumpLoop',
}


def path_of(value):
    return value.get_path_name() if value else None


def bind(asset, bindings, before, after, missing):
    for name, target in bindings.items():
        sequence = unreal.load_asset(target)
        if sequence is None:
            missing.append({'asset': asset.get_path_name(), 'property': name, 'target': target})
            continue
        before.setdefault(asset.get_path_name(), {})[name] = path_of(asset.get_editor_property(name))
        asset.set_editor_property(name, sequence)
        after.setdefault(asset.get_path_name(), {})[name] = path_of(asset.get_editor_property(name))


def main():
    set_asset = unreal.load_asset(SET_PATH)
    assert set_asset is not None, 'DA_RifleLocomotion not found: ' + SET_PATH
    presentation = unreal.load_asset(PRESENTATION_PATH)
    assert presentation is not None, 'AK presentation not found: ' + PRESENTATION_PATH
    before, after, missing = {}, {}, []
    bind(set_asset, BINDINGS, before, after, missing)
    bind(presentation, PRESENTATION_BINDINGS, before, after, missing)
    changed = {a: {k: v for k, v in props.items() if before[a].get(k) != v}
               for a, props in after.items()}
    changed = {a: props for a, props in changed.items() if props}
    for asset in (set_asset, presentation):
        if changed.get(asset.get_path_name()):
            assert LIB.save_loaded_asset(asset), 'save failed: ' + asset.get_path_name()

    # 落盘校验：重新从磁盘加载，确认槽位已持久化。
    verify = {}
    for asset_path, props in after.items():
        reloaded = unreal.load_asset(asset_path)
        verify[asset_path] = {k: path_of(reloaded.get_editor_property(k)) for k in props}
    unpersisted = [{'asset': a, 'property': k, 'disk': v2, 'expected': v}
                   for a, props in after.items() for k, v in props.items()
                   for v2 in [verify[a][k]] if v != v2]

    evidence = {
        'set': SET_PATH,
        'presentation': PRESENTATION_PATH,
        'before': before,
        'after': after,
        'changed': changed,
        'missing_targets': missing,
        'unpersisted': unpersisted,
    }
    out = Path(unreal.Paths.project_saved_dir()) / 'Stinger' / 'AnimationOptimization' / 'ConfigureViewTransitions.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log_warning('STINGER_VIEW_TRANSITIONS changed=%d missing=%d unpersisted=%d' %
                       (len(changed), len(missing), len(unpersisted)))
    for asset, props in changed.items():
        for name, value in props.items():
            unreal.log_warning('STINGER_VIEW_TRANSITION %s.%s -> %s' % (asset, name, value))
    assert not missing, 'missing animation assets: ' + json.dumps(missing, ensure_ascii=False)
    assert not unpersisted, 'unpersisted properties: ' + json.dumps(unpersisted, ensure_ascii=False)


if __name__ == '__main__':
    main()
