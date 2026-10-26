"""Copy the Space Center key card models out of the source dump into RawContent.

The converted assets live inside the split archives on the share, not as loose files:

    \\\\Desktop-m9pa1sf\\三角洲解包资产\\网盘上传压缩包\\可用资源_DeltaForce_Models_006.zip
        └ 可用资源/DeltaForce/Content/Models/Props/Key/props_keycard_Astropark_708/...

Read-only on the share. Run before Scripts/Extraction/import_key_assets.py.
"""
import os
from pathlib import Path
import zipfile

SHARE = Path(r'\\Desktop-m9pa1sf\三角洲解包资产\网盘上传压缩包')
ARCHIVE = SHARE / '可用资源_DeltaForce_Models_006.zip'
PATTERN = 'keycard_Astropark'
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'RawContent/KeyCards'

if not ARCHIVE.is_file():
    raise SystemExit(f'Source archive not reachable: {ARCHIVE}')

OUT.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(ARCHIVE) as archive:
    entries = [n for n in archive.namelist() if PATTERN in n]
    if not entries:
        raise SystemExit(f'No "{PATTERN}" entries in {ARCHIVE.name}')
    for entry in entries:
        target = OUT / os.path.basename(entry)
        target.write_bytes(archive.read(entry))
        print('FETCH_KEY_ASSET', target.name, target.stat().st_size)
print('EXTRACTION_KEY_ASSETS_FETCHED', len(entries))
