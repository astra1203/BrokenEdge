"""把烟雾投掷蒙太奇绑定到 Stinger 角色蓝图。

为什么需要本脚本：
    投掷动画是 C++ 属性（FirstPersonSmokeMontage / SmokeThrowMontage），
    默认值必须落在 BP_StingerCharacter 的类默认值上，游戏内才会生效。

幂等：属性已指向目标蒙太奇时跳过写入，避免每次保存产生无意义的二进制变更。
"""
import unreal

TARGETS = ['/Game/Characters/Stinger/Blueprints/BP_StingerCharacter']
BINDINGS = [
    # FirstPersonSmokeMontage 播到 DedicatedFirstPersonMesh（1P 骨骼），
    # SmokeThrowMontage 播到 GetMesh()（3P 骨骼），两者由 PlayFirstPersonMontage 同步时间轴。
    ('FirstPersonSmokeMontage',
     '/Game/Characters/Stinger/Animations/Extraction/AM_ExtractionSmokeThrow_1P'),
    ('SmokeThrowMontage',
     '/Game/Characters/Stinger/Animations/Extraction/AM_ExtractionSmokeThrow_3P'),
]


def bind(blueprint, prop, asset_path):
    """写入引用；已指向同一资产时不做改动。"""
    asset = unreal.load_asset(asset_path)
    assert asset, 'missing ' + asset_path
    current = blueprint.get_editor_property(prop)
    if current and current.get_path_name().split('.')[0] == asset_path:
        return False
    blueprint.set_editor_property(prop, asset)
    return True


def main():
    changed = 0
    for path in TARGETS:
        generated = unreal.load_object(None, path + '.Default__BP_StingerCharacter_C')
        assert generated, 'cannot resolve class defaults for ' + path
        for prop, asset_path in BINDINGS:
            if bind(generated, prop, asset_path):
                changed += 1
                unreal.log('SMOKE_BIND {0}.{1} <- {2}'.format(path, prop, asset_path))
        package = unreal.load_asset(path)
        assert unreal.EditorAssetLibrary.save_loaded_asset(package), 'save failed ' + path
    print('SMOKE_BIND_DONE changed=' + str(changed))


main()
