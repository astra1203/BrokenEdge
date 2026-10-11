"""Extract traversal clips and original climb actors, never decorative mesh candidates."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from source_ladders import prepare

parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True, help='New source-correction evidence directory')
args = parser.parse_args()
if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
    parser.error('Use an alphanumeric run tag')

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
run = OUT.parent/'SourceCorrection'/args.tag
report = prepare(run)
report['clips'] = clips
report['source_manifest'] = str(run/'Manifest.json')
report['source_manifest_sha256'] = hashlib.sha256((run/'Manifest.json').read_bytes()).hexdigest()
if (OUT.parent/'Sources.json').exists():
    (run/'Sources.before.json').write_bytes((OUT.parent/'Sources.json').read_bytes())
(OUT.parent/'Sources.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print('TRAVERSAL_SOURCES', len(clips), 'clips', len(report['ladders']), 'original ladders', len(report['excluded']), 'outside region')
