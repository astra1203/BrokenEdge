"""把 C101 烟幕枪的 1P/3P 开火动画打成 Montage。

为什么需要本脚本：
    解包资材里的 Support_C101_SmokeScreen_M_{1P,3P}_Fire 是 AnimSequence，
    而角色的 PlayFirstPersonMontage 需要 UAnimMontage 才能驱动 1P/3P 同步播放。

前置：编辑器必须先关闭，本脚本由 UnrealEditor-Cmd 无头执行。

幂等：已存在的 Montage 只把默认槽重新指向同一条序列，保留人工加的 Notify。
"""
import unreal

SEQUENCE_ROOT = '/Game/Characters/Stinger/Animations/Corrected/Mapped'
MONTAGE_ROOT = '/Game/Characters/Stinger/Animations/Extraction'

# 源序列资产名 -> 输出 Montage 名。
PAIRS = [
    ('Support_C101_SmokeScreen_M_1P_Fire', 'AM_ExtractionSmokeThrow_1P'),
    ('Support_C101_SmokeScreen_M_3P_Fire', 'AM_ExtractionSmokeThrow_3P'),
]


def find_sequence(asset_name):
    """在哈希目录下按资产名查找 AnimSequence；同名多份时取路径最短的一个。"""
    paths = unreal.EditorAssetLibrary.list_assets(
        SEQUENCE_ROOT, recursive=True, include_folder=False)
    hits = [p for p in paths if p.split('/')[-1].split('.')[0] == asset_name]
    assert hits, 'missing sequence ' + asset_name
    return sorted(hits, key=len)[0]


def ensure_montage(sequence, montage_path):
    """以 AnimSequence 为源创建或更新 AnimMontage。"""
    if unreal.EditorAssetLibrary.does_asset_exist(montage_path):
        montage = unreal.load_asset(montage_path)
        assert montage, 'cannot load existing ' + montage_path
        tracks = montage.get_editor_property('slot_anim_tracks')
        if tracks:
            tracks[0].set_editor_property('anim_track', sequence)
            montage.set_editor_property('slot_anim_tracks', tracks)
    else:
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property('source_animation', sequence)
        montage = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            montage_path.split('/')[-1], MONTAGE_ROOT, unreal.AnimMontage, factory)
        assert montage, 'failed to create ' + montage_path
    assert unreal.EditorAssetLibrary.save_loaded_asset(montage), 'save failed ' + montage_path
    return montage


def main():
    count = 0
    for asset_name, montage_name in PAIRS:
        sequence_path = find_sequence(asset_name)
        sequence = unreal.load_asset(sequence_path)
        assert sequence, 'cannot load ' + sequence_path
        montage = ensure_montage(sequence, MONTAGE_ROOT + '/' + montage_name)
        unreal.log('SMOKE_MONTAGE {0} <- {1} len={2:.2f}s'.format(
            montage_name, sequence_path, montage.get_play_length()))
        count += 1
    print('SMOKE_MONTAGE_DONE ' + str(count))


main()
