"""Bounded read-only extraction of traversal clips and the 39 restored ladder candidates."""
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/Traversal/Source'
ARCHIVES = Path('D:/司晨/源代码和素材/三角洲解包资产/网盘上传压缩包')
OUT.mkdir(parents=True, exist_ok=True)
clips = []
for number in (7, 9):
    archive = next(ARCHIVES.glob(f'*Models_{number:03}.zip'))
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            name = Path(entry.filename).name
            if not name.endswith('.psa') or '_Curve' in name:
                continue
            wanted = number == 7 and name.startswith('AR_M_3P_Climb') or number == 9 and name.startswith(('Unarmed_M_1P_ClimbLadder', 'Unarmed_M_3P_ClimbLadder'))
            if not wanted or any(c['name'] == name for c in clips):
                continue
            assert entry.file_size < 128 * 1024**2
            data = z.read(entry)
            target = OUT / name
            target.write_bytes(data)
            clips.append(dict(name=name, archive=archive.name, entry=entry.filename,
                              bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), file=str(target)))
active = json.loads((ROOT/'Scripts/SpaceCenter/restoration_active.json').read_text(encoding='utf-8'))
manifest_path = ROOT / active['manifest_file']
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
assert hashlib.sha256(manifest_path.read_bytes()).hexdigest() == active['manifest_sha256']
ladders = [a for a in manifest['actors'] if 'ladder' in a['mesh'].lower()]
assert len(ladders) == 39
report = dict(clips=clips, origin=manifest['origin'], ladders=ladders,
              manifest_sha256=active['manifest_sha256'], meshes=manifest['meshes'])
(OUT.parent/'Sources.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print('TRAVERSAL_SOURCES', len(clips), 'clips', len(ladders), 'ladders', sum(c['bytes'] for c in clips), 'bytes')
