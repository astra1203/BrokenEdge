"""Offline: pick an official icon PNG for every extraction item and extract it.

    <engine python> -I Scripts/Extraction/resolve_item_icons.py

Sources (可用资源_DeltaForce_UI_001.zip, UI/UIIcon):
- ItemIcon/<w>_<h>/Icon_<official id>.png or Icon_<prop model>.png -- props, meds, ammo, keys
- ItemIcon_RTI/<preset>_<receiver>.png / <part>_<part>.png -- rendered guns and attachments
- ItemIcon/0_0/*_LOW|MID|HIGH.png -- generic tier icons
Collectibles resolve through CommonItemAssets_Prop: item id -> UI/dropped mesh name -> icon.
Rigs, backpacks and safes have no per-item icon in the unpack (the game renders them), so
they use the generic tier icons; collectibles without an icon fall back to MATERIAL_<tier>.

Writes Saved/Extraction/Icons/<item id>.png and Scripts/Extraction/data/item_icons.json
(read by import_item_icons.py inside the editor).
"""
import json
import os
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [r'D:\司晨\源代码和素材\三角洲解包资产\网盘上传压缩包',
           r'\\Desktop-m9pa1sf\三角洲解包资产\网盘上传压缩包']
OUT = ROOT / 'Saved/Extraction/Icons'
TIER = ['LOW', 'LOW', 'MID', 'MID', 'HIGH', 'HIGH']

# project item id -> icon stem (case-insensitive file name without .png)
FIXED = {
    'AK': '10010000012_18010000006', 'M4A1': '10010000122_18010000001', 'SCARH': '10010000228_18010000021',
    'Ammo': 'Icon_Bullets_762X39_1347', 'AmmoAP': 'Icon_Bullets_762X39_1438',
    'Ammo76251': 'Icon_Bullets_762x51_1552', 'Ammo55645': 'Icon_Bullets_556X45_1461',
    'Medkit': 'Icon_props_Med_650', 'Bandage': 'Icon_props_Med_1077', 'Splint': 'Icon_props_Med_653',
    'Helmet2': 'HELMET_MID', 'Helmet3': 'HELMET_HIGH', 'Vest2': 'ARMOUR_MID', 'Vest3': 'ARMOUR_HIGH',
    'RedDot': '13110000024_13110000024', 'VerticalGrip': '13140000021_13140000021',
    'Electronics': 'Icon_props_collect_graphicscard_946', 'Tools': 'Icon_props_collect_toolbox_1095',
    'Document': 'Icon_props_collect_longfile_1144', 'FragGrenade': 'Icon_Thr_Grenade_M67_001_A04',
    'Key': 'Icon_props_keycard_Astropark_708', 'Key_Contract': 'Icon_props_keycard_Astropark_708',
    'Key_StaffPassage': 'Icon_props_keycard_Astropark_708', 'Key_EastBridge': 'Icon_props_keycard_Astropark_708',
    'Key_Monitor': 'Icon_props_keycard_Astropark_708', 'Key_President': 'Icon_props_keycard_Astropark_Gold_712',
    'Key_Core': 'Icon_props_keycard_Astropark_Gold_712',
    'Rig_Pouch': 'SUIT_LOW', 'Rig_Ammo': 'SUIT_LOW', 'Rig_Assault': 'SUIT_MID', 'Rig_Hurricane': 'SUIT_HIGH',
    'Bag_Climb': 'SUIT_LOW', 'Bag_Hike': 'SUIT_MID', 'Bag_ALS': 'SUIT_HIGH', 'Bag_GTO': 'SUIT_HIGH',
    'Safe_Basic': 'SupplyBox_Low', 'Safe_Advanced': 'SupplyBox_Mid', 'Safe_Premium': 'SupplyBox_High',
}


def main():
    source = next((s for s in SOURCES if os.path.isdir(s)), SOURCES[0])
    ui = zipfile.ZipFile(os.path.join(source, '可用资源_DeltaForce_UI_001.zip'))
    icons = {}
    for name in ui.namelist():
        if '/UIIcon/ItemIcon' in name and name.endswith('.png'):
            icons.setdefault(os.path.splitext(os.path.basename(name))[0].lower(), name)
    tables = zipfile.ZipFile(os.path.join(source, '可用资源_DeltaForce_DataTables_001.zip'))
    member = next(n for n in tables.namelist() if n.endswith('/CommonItemAsset/CommonItemAssets_Prop.json'))
    props = json.loads(tables.read(member))['Rows']
    parts = json.loads(tables.read(next(n for n in tables.namelist()
                                        if n.endswith('/CommonItemAsset/CommonItemAssets_Weapon.json'))))['Rows']

    def from_prop(official):
        if 'icon_' + official in icons:
            return icons['icon_' + official]
        for key in ('ItemUIMeshPath', 'ItemDroppedMeshPath', 'Item1PMeshPath', 'Item3PMeshPath'):
            path = ((props.get(official) or {}).get(key) or {}).get('AssetPathName', 'None')
            if path == 'None':
                continue
            stem = re.sub(r'(_(1p|3p|ui|lay|skin))+$', '', path.rsplit('/', 1)[-1].split('.')[0].lower())
            for candidate in ('icon_' + stem, stem + '_icon', stem, stem + '_ui', 'icon_' + stem + '_ui'):
                if candidate in icons:
                    return icons[candidate]
            number = re.search(r'_(\d{3,4})$', stem)
            if number:
                hits = sorted(v for k, v in icons.items() if re.search('_%s(_|$)' % number.group(1), k))
                if hits:
                    return hits[0]
        return None

    plan = {item: icons.get(stem.lower()) for item, stem in FIXED.items()}
    # Compensator: first rendered 1313 part whose weapon-asset name says compensator.
    plan['Compensator'] = next((icons[k] for k in sorted(icons) if k.startswith('1313') and re.search(
        r'comp', json.dumps(parts.get(k.split('_')[0], {})).lower())), None) or icons.get('13130000148_13130000148')
    collectibles = json.loads((ROOT / 'Scripts/Extraction/data/spacecenter_collectibles.json')
                              .read_text(encoding='utf-8'))['items']
    fallback = 0
    for row in collectibles:
        found = from_prop(row['official_id'])
        if not found:
            found = icons['material_' + TIER[row['rarity']].lower()]
            fallback += 1
        plan[row['id']] = found
    missing = sorted(k for k, v in plan.items() if not v)
    assert not missing, missing
    OUT.mkdir(parents=True, exist_ok=True)
    for item, member_name in plan.items():
        (OUT / (item + '.png')).write_bytes(ui.read(member_name))
    data = ROOT / 'Scripts/Extraction/data/item_icons.json'
    data.write_text(json.dumps({'source': 'UI_001.zip', 'icons': plan}, ensure_ascii=False, indent=1) + '\n',
                    encoding='utf-8')
    print('ITEM_ICONS_RESOLVED', len(plan), 'collectible_fallback', fallback)


if __name__ == '__main__':
    main()
