"""Extract official PC looting/HUD widget blueprints as JSON layout references (source ZIPs read-only).

    <engine python> Scripts/Extraction/prepare_ui_reference.py
    dotnet Scripts/SpaceCenter/Converter/bin/Release/net10.0/SpaceCenter.Converter.dll Saved/Extraction/UIReference/inspect-request.json

Packages come from 原始资源ZIP/10_界面字体_001.zip through Saved/SpaceCenter/catalog.sqlite.
Only the widget packages are extracted (no recursive textures); the inspect output holds the
widget tree, canvas slot layout, brushes and fonts used to rebuild the project UI.
"""
import json
from pathlib import Path
import sqlite3
import sys

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'Scripts/SpaceCenter'))
from source_packages import extract

OUT = PROJECT / 'Saved/Extraction/UIReference'
BASE = 'DeltaForce/Content/BluePrints/'
NAMES = [
    'UI_HD/UMG/System/Looting/WBP_LootingEquipPanel_Pc_V3',
    'UI_HD/UMG/System/Looting/ContainerAndItem/WBP_LootingBackpackV2_Pc',
    'UI_HD/UMG/System/Looting/WBP_LootingPickupView_Pc',
    'UI_HD/UMG/System/Looting/WBP_LootingMezzanine_PC',
    'UI/UMG/System/Looting/ContainerAndItem/WBP_LootingSlotView',
    'UI/UMG/System/Looting/ContainerAndItem/WBP_LootingContainer',
    'UI/UMG/System/Looting/ContainerAndItem/WBP_LootingEquipment',
    'UI/UMG/Common/SlotItemView/WBP_WarehouseItemTemplate',
    'UI/UMG/Common/SlotItemView/WBP_HUDItemTemplate',
    'UI/UMG/Common/SlotItemView/Component/WBP_SlotCompBg',
    'UI/UMG/Common/SlotItemView/Component/WBP_SlotCompQuality',
    'UI/UMG/Common/SlotItemView/Component/WBP_SlotCompIconImage',
    'UI/UMG/Common/SlotItemView/Component/WBP_SlotCompIconLabel',
    'UI/UMG/Common/ItemDetail/WBP_ItemDetailForLoot',
]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect((PROJECT / 'Saved/SpaceCenter/catalog.sqlite').resolve().as_uri() + '?mode=ro', uri=True)
    selected, missing = [], []
    for name in NAMES:
        row = db.execute("select path from entries where package=? and ext='.uasset'",
                         ((BASE + name).lower(),)).fetchone()
        if row:
            selected.append(row[0][:-7])
        else:
            missing.append(name)
    db.close()
    report = extract(selected, recursive=False, limit_gib=1)
    request = dict(mode='inspect', format='ActorX', source=str(PROJECT / 'Saved/SpaceCenter/Source/DeltaForce'),
                   output=str(OUT / 'inspect'), packages=selected)
    (OUT / 'inspect-request.json').write_text(json.dumps(request, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'selected': len(selected), 'missing': missing, 'report': report.get('report')},
                     ensure_ascii=False))


if __name__ == '__main__':
    main()
