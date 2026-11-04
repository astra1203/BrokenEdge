"""Render the project-original item icons, import them and assign one to every item definition.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/generate_original_icons.py" -NullRHI -unattended -nosound

Designs live in original_icon_shapes.py (flat silhouettes, no third-party artwork). Each design becomes one
texture /Game/Extraction/Items/Icons/T_ItemIcon_<design>; items pick a design from their kind, equipment
slot and name keywords. Textures of the previous icon set (T_Icon_*) are deleted. Idempotent.
"""
import json
import sys
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir())
sys.path.insert(0, str(ROOT / 'Scripts/Extraction'))
import original_icon_shapes as shapes  # noqa: E402

PNG = ROOT / 'Saved/Extraction/OriginalIcons'
FOLDER = '/Game/Extraction/Items/Icons'
ITEMS = '/Game/Extraction/Items'
lib = unreal.EditorAssetLibrary

# 名称关键字 -> 图标设计，按顺序匹配第一个命中的。
VALUABLE_KEYWORDS = [
    (('水泥', '木板', '纤维', '钢', '碳纤维'), 'crate'),
    (('注射器',), 'syringe'),
    (('钥匙',), 'key'),
    (('通行证', '工牌'), 'keycard'),
    (('瓦斯', '氧气', '试管', '冷却剂', '氟化液', '喷雾', '滤毒罐'), 'bottle'),
    (('手套', '敷料', '除颤'), 'medkit'),
    (('扑克',), 'card'),
    (('币', '勋章', '饰章'), 'coin'),
    (('像', '化石', '木雕', '模型', '展品', '陶瓷', '墙饰', '鼓', '碟', '座钟', '烟斗'), 'statue'),
    (('电线', '网线', '电缆', '插座', '软管', '胶带', '胶布'), 'cable'),
    (('炮弹', '炸药', '火药', '燃气罐'), 'grenade'),
    (('罐头', '可乐', '咖啡', '茶', '水壶', '纯净水', '酸奶', '蛋白粉', '能量', '面', '苹果', '凝胶', '糖', '调料',
      '豆', '泡腾', '香槟', '牙膏'), 'can'),
    (('文件', '资料', '情报', '笔记', '档案', '图纸', '报告', '手册', '日志', '书', '周刊', '月刊', '数据'), 'document'),
    (('芯片', '电路', '主板', '硬盘', '显卡', 'CPU', '处理器', '内存', '存储', '固态', 'U盘', '电容'), 'chip'),
    (('金条', '银条', '金砖', '金块', '金锭'), 'bar'),
    (('宝石', '钻石', '项链', '戒指', '手表', '怀表', '珠宝', '翡翠', '玉', '吊坠', '耳环', '王冠', '徽章', '奖章'), 'gem'),
    (('酒', '瓶', '试剂', '药剂', '溶液', '香水', '燃料', '油'), 'bottle'),
    (('电脑', '平板', '手机', '相机', '终端', '雷达', '电台', '对讲', '显示', '仪', '机', '屏', '设备', '电池', '发电',
      '器', '计', '服务', '路由', '天线', '电源', '鼠标', '键盘', '音响', '声卡', '光驱', '手柄', '磁盘', '超算', '摄像',
      '镜头', '炉', '灯', '录音', '太阳能', '笔', '额温枪'), 'device'),
    (('扳手', '钳', '锤', '螺丝', '工具', '刀', '锯', '钻', '镊子', '尺', '钉', '胶', '喷漆', '喷灯', '零件', '火柴'), 'tool'),
]
ATTACHMENT_KEYWORDS = [
    (('镜', '瞄'), 'scope'),
    (('握把', '把手'), 'grip'),
    (('消音', '消声', '枪口', '制退', '补偿', '消焰'), 'muzzle'),
    (('弹匣', '弹鼓', '弹夹'), 'magazine'),
]
MEDICAL_KEYWORDS = [
    (('绷带', '纱布', '止血'), 'bandage'),
    (('夹板', '固定'), 'splint'),
    (('针', '注射', '激素', '肾上腺'), 'syringe'),
    (('药', '片', '胶囊'), 'pills'),
]


def match(label, table, fallback):
    for words, design in table:
        if any(w in label for w in words):
            return design
    return fallback


def design_for(definition):
    kind = definition.get_editor_property('kind').name.upper()
    label = str(definition.get_editor_property('label'))
    if kind == 'WEAPON':
        return 'rifle'
    if kind == 'AMMO':
        return 'ammo'
    if kind == 'MEDICAL':
        return match(label, MEDICAL_KEYWORDS, 'medkit')
    if kind == 'ARMOR':
        return 'helmet' if definition.get_editor_property('armor_slot') == 2 else 'vest'
    if kind == 'GEAR':
        slot = definition.get_editor_property('gear_slot')
        return {4: 'rig', 5: 'backpack', 6: 'safe'}.get(slot, 'backpack')
    if kind == 'ATTACHMENT':
        return match(label, ATTACHMENT_KEYWORDS, 'rail')
    if kind == 'KEY':
        return 'keycard' if '卡' in label else 'key'
    if kind == 'THROWABLE':
        return 'grenade'
    return match(label, VALUABLE_KEYWORDS, 'crate')


def main():
    PNG.mkdir(parents=True, exist_ok=True)
    definitions = {}
    for path in lib.list_assets(ITEMS, recursive=False):
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.ExtractionItemDefinition):
            definitions[asset.get_name()] = asset
    plan = {name: design_for(d) for name, d in definitions.items()}

    tasks = []
    for design in sorted(set(plan.values())):
        width, height, data = shapes.render(design)
        file = PNG / (design + '.png')
        shapes.write_png(str(file), width, height, data)
        task = unreal.AssetImportTask()
        task.filename = str(file)
        task.destination_path = FOLDER
        task.destination_name = 'T_ItemIcon_' + design
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    textures = {}
    for design in sorted(set(plan.values())):
        texture = unreal.load_asset('%s/T_ItemIcon_%s' % (FOLDER, design))
        assert texture, design
        texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property('never_stream', True)
        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        assert lib.save_loaded_asset(texture, only_if_is_dirty=False), design
        textures[design] = texture

    for name, definition in definitions.items():
        icon = definition.get_editor_property('icon')
        if icon and icon.get_name().startswith('T_ItemIcon_W_'):
            continue  # 枪械图标由 generate_weapon_icons.py 按枪模生成，保留
        definition.set_editor_property('icon', textures[plan[name]])
        assert lib.save_loaded_asset(definition, only_if_is_dirty=False), name

    removed = 0
    for path in lib.list_assets(FOLDER, recursive=False):
        if path.split('/')[-1].split('.')[0].startswith('T_Icon_'):
            assert lib.delete_asset(path), path
            removed += 1

    counts = {}
    for design in plan.values():
        counts[design] = counts.get(design, 0) + 1
    fallback = [str(definitions[n].get_editor_property('label')) for n, d in plan.items() if d == 'crate']
    print('ORIGINAL_ICONS', json.dumps({'items': len(plan), 'designs': counts, 'removed_old': removed,
                                        'crate_labels': fallback}, ensure_ascii=False))


main()
