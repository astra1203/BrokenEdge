"""Record registry automation and the current military layout navigation audit."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
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
environment = dict(os.environ, EXTRACTION_RUN_DIR=str(out), UE_SKIP_UBT_SDK_SETUP='1')
for name, options in [
    ('Registry', ['-ExecCmds=Automation RunTests BrokenEdge.Extraction.Maps',
                  '-TestExit=Automation Test Queue Empty', '-ReportExportPath=' + str(out / 'Automation')]),
    ('Navigation', ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/validate_soviet_layout.py')]),
]:
    command = [str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'), str(ROOT / 'BrokenEdge.uproject'),
               '-NullRHI', '-unattended', '-nosound', '-SCCProvider=None', '-abslog=' + str(out / (name + '.log'))] + options
    print('SOVIET_AUDIT_STARTED', name, flush=True)
    step = dict(name=name, command=command, exit_code=None, result='running',
                started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    report['steps'].append(step)
    (out / 'Run.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    with (out / (name + '-process.log')).open('wb') as log:
        proc = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        step['pid'] = proc.pid
        (out / 'Run.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        try:
            code = proc.wait(timeout=900)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], capture_output=True)
            code = proc.wait(timeout=30)
            step['error'] = 'Audit step timed out'
    text = (out / (name + '.log')).read_text(encoding='utf-8', errors='replace')
    passed = code == 0 and 'Fatal error:' not in text and 'Traceback (most recent call last)' not in text and 'error' not in step
    if name == 'Registry':
        passed &= len(re.findall(r'Test Completed\. Result=\{(?:Success|\u6210\u529f)\}', text)) == 1
        passed &= not bool(re.search(r'Test Completed\. Result=\{(?:Fail|Failure|\u5931\u8d25)', text))
    else:
        passed &= 'SOVIET_NAVIGATION_AUDIT {"points": 77, "failed": []}' in text
        try:
            audit = json.loads((out / 'NavigationAudit.json').read_text(encoding='utf-8'))
            passed &= len(audit['open']) == 77 and len(audit['closed']) == 77 and all(p['reachable'] is True for p in audit['open'])
        except (OSError, ValueError, KeyError, TypeError) as error:
            passed = False
            step['error'] = str(error)
    step.update(exit_code=code, passed=bool(passed), result='passed' if passed else 'failed',
                finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    report['result'] = 'failed' if not passed else 'passed' if len(report['steps']) == 2 else 'running'
    report['exit_code'] = None if report['result'] == 'running' else 0 if passed else (code or 1)
    if report['result'] != 'running':
        report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out / 'Run.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('SOVIET_AUDIT_FINISHED', name, passed, flush=True)
    if not passed:
        raise SystemExit(1)
