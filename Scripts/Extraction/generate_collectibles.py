"""Offline: pull the official Space Center collectibles out of GameItem.

    <engine python> -I Scripts/Extraction/generate_collectibles.py [--source <archives dir>]

Source: `R13N/Common/Base/DataTables/GameItem.json` in 可用资源_DeltaForce_R13N_001.zip.
Every row whose `SourceMapId` contains 3902 (航天基地) and whose id starts with 1501-1509
(the collectible families) becomes one item `Col_<official id>`:
    label = Name, size = (Length, Width), stack = MaxStackCount, rarity = Quality - 1,
    sell value = InitialGuidePrice / PRICE_DIVISOR (project economy, AK costs 1200).
1520 rows (task/event items such as 商业机密) are left out.

Writes:
    Source/BrokenEdge/Extraction/ExtractionCollectibles.inl  (whitelist entries)
    Scripts/Extraction/data/spacecenter_collectibles.json    (read by configure_collectibles.py)
"""
import argparse
import json
import os
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [r'D:\司晨\源代码和素材\三角洲解包资产\网盘上传压缩包',
           r'\\Desktop-m9pa1sf\三角洲解包资产\网盘上传压缩包']
MAP_ID = 3902
PRICE_DIVISOR = 50
MIN_VALUE = 10
# id prefix -> DisplayCategory, used by loot recipe pools
CATEGORIES = {'1501': 'Household', '1502': 'Tools', '1503': 'Electronics', '1504': 'Misc',
              '1505': 'Misc', '1506': 'Medical', '1507': 'Intel', '1508': 'Collectible', '1509': 'Collectible'}


def text(value):
    return ((value or {}).get('LocalizedString') or (value or {}).get('SourceString') or '').strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', default=next((s for s in SOURCES if os.path.isdir(s)), SOURCES[0]))
    args = parser.parse_args()
    archive = zipfile.ZipFile(os.path.join(args.source, '可用资源_DeltaForce_R13N_001.zip'))
    member = next(n for n in archive.namelist() if n.endswith('/Base/DataTables/GameItem.json'))
    rows = json.loads(archive.read(member))['Rows']
    items = []
    for key, row in sorted(rows.items()):
        if MAP_ID not in (row.get('SourceMapId') or []) or key[:4] not in CATEGORIES:
            continue
        name = text(row['Name'])
        if not name:
            continue
        items.append({
            'id': 'Col_' + key, 'official_id': key, 'label': name,
            'category': CATEGORIES[key[:4]],
            'size': [max(1, int(row['Length'])), max(1, int(row['Width']))],
            'max_stack': max(1, int(row['MaxStackCount'])),
            'rarity': max(0, min(5, int(row['Quality']) - 1)),
            'official_price': int(row['InitialGuidePrice']),
            'sell_value': max(MIN_VALUE, round(int(row['InitialGuidePrice']) / PRICE_DIVISOR)),
            'description': text(row.get('Description')),
        })
    data = ROOT / 'Scripts/Extraction/data'
    data.mkdir(exist_ok=True)
    (data / 'spacecenter_collectibles.json').write_text(
        json.dumps({'source': member, 'map_id': MAP_ID, 'price_divisor': PRICE_DIVISOR, 'items': items},
                   ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
    lines = ['// 由 Scripts/Extraction/generate_collectibles.py 生成，勿手改。航天基地官方收集品白名单。']
    lines += ['TEXT("%s"),' % item['id'] for item in items]
    (ROOT / 'Source/BrokenEdge/Extraction/ExtractionCollectibles.inl').write_text(
        '\n'.join(lines) + '\n', encoding='utf-8')
    print('COLLECTIBLES_GENERATED', len(items))


if __name__ == '__main__':
    main()
