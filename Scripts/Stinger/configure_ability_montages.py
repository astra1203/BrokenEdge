"""为蜂医技能（治疗/救援/烟雾）创建 1P+3P 表现蒙太奇并绑定到角色蓝图。

用法（编辑器关闭）：
  UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript ^
      -script="<绝对路径>/Scripts/Stinger/configure_ability_montages.py" -NullRHI -unattended -nosound
证据写入 Saved/Stinger/AnimationOptimization/ConfigureAbilityMontages.json。
"""
import json
from pathlib import Path
import unreal

LIB = unreal.EditorAssetLibrary
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MAPPED = '/Game/Characters/Stinger/Animations/Corrected/MappedV4'
DEST = '/Game/Characters/Stinger/Animations/Abilities'
BLUEPRINTS = ('BP_StingerCharacter', 'BP_StingerCharacter_Repaired')
BP_ROOT = '/Game/Characters/Stinger/Blueprints/'

# 每个技能一对（第一人称 / 第三人称）；源序列都取已重映射到目标骨架的 MappedV4 版本。
SOURCES = {
    'Heal': (MAPPED + '/0b5a0cec/Support_C101_MedicalGun_M_1P_UseForMe',
             MAPPED + '/452b0376/Support_C101_MedicalGun_M_3P_UesForMe'),
    # 激素枪射向队友：官方 MedicalGun 单发 AimFire（1P）与 Fire（3P）。
    'HealOther': (MAPPED + '/1ae74b90/Support_C101_MedicalGun_M_1P_AimFire',
                  MAPPED + '/6c8b92e4/Support_C101_MedicalGun_M_3P_Fire'),
    'Revive': (MAPPED + '/aee6cf12/Support_C101_MedicalGun_M_1P_Rescue_Loop',
               MAPPED + '/2174ae28/Support_C101_MedicalGun_M_3P_Rescue_Loop'),
    'Smoke': (MAPPED + '/3d9c73aa/Support_C101_SmokeScreen_M_1P_Fire',
              MAPPED + '/9a2b585d/Support_C101_SmokeScreen_M_3P_Fire'),
}
# 蓝图属性名 -> 蒙太奇资产名
PROPERTIES = {
    'Heal': ('FirstPersonHealMontage', 'HealMontage'),
    'HealOther': ('FirstPersonHealOtherMontage', 'HealOtherMontage'),
    'Revive': ('FirstPersonReviveMontage', 'ReviveMontage'),
    'Smoke': ('FirstPersonSmokeMontage', 'SmokeMontage'),
}


def montage_source(montage):
    """读出蒙太奇第一段的源序列；结构异常时返回 None，交由调用方重建。"""
    try:
        tracks = montage.get_editor_property('slot_anim_tracks')
        if not tracks:
            return None
        segments = tracks[0].get_editor_property('anim_track').get_editor_property('anim_segments')
        if not segments:
            return None
        return segments[0].get_editor_property('anim_reference')
    except Exception:
        return None


def build_montage(sequence, name):
    path = DEST + '/' + name
    if LIB.does_asset_exist(path):
        montage = LIB.load_asset(path)
        if montage is not None and montage_source(montage) == sequence:
            return montage, 'reused'
        # 源序列不同则重建，避免留下过期绑定。
        assert LIB.delete_asset(path), 'delete failed: ' + path
    factory = unreal.AnimMontageFactory()
    factory.set_editor_property('source_animation', sequence)
    factory.set_editor_property('target_skeleton', sequence.get_editor_property('skeleton'))
    montage = ASSET_TOOLS.create_asset(name, DEST, unreal.AnimMontage, factory)
    if montage is None:
        montage = LIB.load_asset(path)
        if montage is None:
            raise ValueError('montage create failed: ' + path)
    assert montage_source(montage) == sequence, 'montage source mismatch: ' + path
    return montage, 'created'


def save_blueprint(bp, blueprint):
    """改 CDO 不会自动把蓝图包标脏，用 only_if_is_dirty=False 强制落盘。"""
    return LIB.save_loaded_asset(bp, only_if_is_dirty=False)


def main():
    # create_asset 会自动建立缺少的目录，无需预先 mkdir。
    built, problems = {}, []
    for action, (view_path, body_path) in SOURCES.items():
        for label, source_path, suffix in (('view', view_path, '1P'), ('body', body_path, '3P')):
            sequence = unreal.load_asset(source_path)
            if sequence is None:
                problems.append({'action': action, 'role': label, 'missing': source_path})
                continue
            montage, state = build_montage(sequence, 'AM_Ability_%s_%s' % (action, suffix))
            assert montage is not None
            assert LIB.save_loaded_asset(montage), 'save failed: ' + montage.get_path_name()
            built.setdefault(action, {})[label] = {
                'montage': montage.get_path_name(),
                'length': montage.get_play_length(),
                'state': state,
                'source': sequence.get_path_name(),
            }

    bound, unpersisted = {}, []
    for blueprint in BLUEPRINTS:
        bp = LIB.load_asset(BP_ROOT + blueprint)
        if bp is None:
            problems.append({'blueprint': blueprint, 'missing': True})
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        changed = {}
        for action, (view_prop, body_prop) in PROPERTIES.items():
            entry = built.get(action)
            if entry is None:
                continue
            for prop, role in ((view_prop, 'view'), (body_prop, 'body')):
                target = LIB.load_asset(entry[role]['montage'])
                current = cdo.get_editor_property(prop)
                if current != target:
                    cdo.set_editor_property(prop, target)
                    changed[prop] = entry[role]['montage']
                    unreal.log_warning('STINGER_ABILITY_SET %s.%s -> %s' % (blueprint, prop,
                                                                           entry[role]['montage']))
        if changed:
            assert save_blueprint(bp, blueprint), (
                'save failed: %s —— 编辑器打开时 uasset 被占用（Windows 共享冲突），请先关闭编辑器' % blueprint)
        # 落盘校验：重新加载蓝图 CDO 比对。
        reloaded = unreal.get_default_object(LIB.load_asset(BP_ROOT + blueprint).generated_class())
        for action, (view_prop, body_prop) in PROPERTIES.items():
            for prop in (view_prop, body_prop):
                value = reloaded.get_editor_property(prop)
                if value is None:
                    unpersisted.append({'blueprint': blueprint, 'property': prop, 'value': None})
        bound[blueprint] = {prop: (reloaded.get_editor_property(prop).get_path_name()
                                  if reloaded.get_editor_property(prop) else None)
                            for pair in PROPERTIES.values() for prop in pair}

    evidence = {
        'montages': built,
        'bound': bound,
        'problems': problems,
        'unpersisted': unpersisted,
    }
    out = (Path(unreal.Paths.project_saved_dir()) / 'Stinger' / 'AnimationOptimization'
           / 'ConfigureAbilityMontages.json')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log_warning('STINGER_ABILITY_MONTAGES actions=%d problems=%d unpersisted=%d' %
                       (len(built), len(problems), len(unpersisted)))
    for action, entry in built.items():
        for role, info in entry.items():
            unreal.log_warning('STINGER_ABILITY_MONTAGE %s.%s -> %s (%.3fs, %s)' %
                               (action, role, info['montage'], info['length'], info['state']))
    assert not problems, 'problems: ' + json.dumps(problems, ensure_ascii=False)
    assert not unpersisted, 'unpersisted: ' + json.dumps(unpersisted, ensure_ascii=False)


if __name__ == '__main__':
    main()
