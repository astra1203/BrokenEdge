"""Record registry automation and the current military layout navigation audit."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Common.ue_env import engine_root, run_record
parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True)
parser.add_argument('--engine', type=Path)
args = parser.parse_args()
try:
    ENGINE = engine_root(args.engine)
except (ValueError, OSError, KeyError) as error:
    parser.error(str(error))
if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
    raise ValueError('Use an alphanumeric run tag')
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
report = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), steps=[], result='running')
report.update(run_record([sys.executable, __file__, *sys.argv[1:]], out / 'Run.json', engine=ENGINE, script=__file__))
report['input_sha256'] = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in [
    'Content/Maps/SovietMilitary/L_SovietMilitaryExtraction.umap',
    'Content/Extraction/Maps/DA_SovietMilitaryGameplay.uasset',
    'Content/Extraction/Loot/DA_SovietMilitaryLoot.uasset',
    'Scripts/Extraction/validate_soviet_layout.py',
    'Source/BrokenEdge/Tests/ExtractionMapTests.cpp',
]}
(out / 'Run.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
for name, options in [
    ('Registry', ['-ExecCmds=Automation RunTests BrokenEdge.Extraction.Maps',
                  '-TestExit=Automation Test Queue Empty', '-ReportExportPath=' + str(out / 'Automation')]),
    ('Navigation', ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/validate_soviet_layout.py')]),
]:
    command = [str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'), str(ROOT / 'BrokenEdge.uproject'),
               '-NullRHI', '-unattended', '-nosound', '-SCCProvider=None', '-abslog=' + str(out / (name + '.log'))] + options
    print('SOVIET_AUDIT_STARTED', name, flush=True)
    with (out / (name + '-process.log')).open('wb') as log:
        code = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                              creationflags=subprocess.CREATE_NO_WINDOW).returncode
    text = (out / (name + '.log')).read_text(encoding='utf-8', errors='replace')
    passed = code == 0 and 'Fatal error:' not in text
    if name == 'Registry':
        passed &= len(re.findall(r'Test Completed\. Result=\{(?:Success|\u6210\u529f)\}', text)) == 1
    else:
        passed &= 'SOVIET_NAVIGATION_AUDIT {"points": 77, "failed": []}' in text
        audit = ROOT / 'Saved/Extraction/SovietMilitary/NavigationAudit.json'
        if audit.exists():
            shutil.copy2(audit, out / audit.name)
    report['steps'].append(dict(name=name, command=command, exit_code=code, passed=bool(passed)))
    report['result'] = 'failed' if not passed else 'passed' if len(report['steps']) == 2 else 'running'
    report['exit_code'] = None if report['result'] == 'running' else 0 if passed else (code or 1)
    report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out / 'Run.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('SOVIET_AUDIT_FINISHED', name, passed, flush=True)
    if not passed:
        raise SystemExit(1)
