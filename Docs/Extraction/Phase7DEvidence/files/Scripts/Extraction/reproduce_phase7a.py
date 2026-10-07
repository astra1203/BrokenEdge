"""Repeat conversion and cached asset import, proving source IDs and bytes stable."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True)
args = parser.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
work = ROOT / 'Saved/SpaceCenter/Phase7A'


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def snapshot():
    paths = [p for p in (ROOT / 'Content/Maps/SpaceCenter').rglob('*') if p.is_file()]
    paths += [work / n for n in ['ImportManifest.json', 'Selection01.json', 'GeometryAudit.json']]
    manifest = json.loads((work / 'ImportManifest.json').read_text(encoding='utf-8'))
    paths += [Path(entry['file']) for kind in ['meshes', 'textures'] for entry in manifest[kind].values()]
    return {str(p.relative_to(ROOT)): sha(p) for p in paths}


before = snapshot()
backup = out / 'NativeBefore'
for relative in before:
    if Path(relative).parts[0] == 'Content':
        destination = backup / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / relative, destination)
environment = os.environ.copy()
environment.update(SPACECENTER_WORK=str(work), SPACECENTER_CONFIG=str(ROOT / 'Scripts/SpaceCenter/north_config.json'))
report = {'result': 'running', 'before': before, 'native_backup': str(backup.relative_to(ROOT))}
try:
    subprocess.run([sys.executable, str(ROOT / 'Scripts/SpaceCenter/run_conversion.py'), '--tag', args.tag],
                   cwd=ROOT, env=environment, check=True)
    subprocess.run([sys.executable, str(ROOT / 'Scripts/Extraction/run_editor_task.py'), 'north-import', '--tag', args.tag + '-import'],
                   cwd=ROOT, env=environment, check=True)
    after = snapshot()
    report['after'] = after
    report['differences'] = [p for p in before.keys() | after.keys() if before.get(p) != after.get(p)]
    assert not report['differences'], report['differences']
    subprocess.run([sys.executable, str(ROOT / 'Scripts/Extraction/run_editor_task.py'), 'phase7a-audit', '--tag', args.tag + '-audit'],
                   cwd=ROOT, env=environment, check=True)
    report['result'] = 'passed'
except Exception as error:
    report.update(result='failed', error=repr(error))
    raise
finally:
    (out / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
