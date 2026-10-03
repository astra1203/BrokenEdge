"""Recorded isolated editor tasks for Phase 3 assets and inventory rule tests."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path('C:/UnrealProjects/UnrealEngine-5.8.3-release')
parser = argparse.ArgumentParser()
parser.add_argument('task', choices=['assets', 'exit', 'inventory', 'persistence', 'recover-existing'])
parser.add_argument('--tag', required=True)
parser.add_argument('--store', type=Path, help='Existing store directory; only for recover-existing')
args = parser.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
command = [str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'), str(ROOT / 'BrokenEdge.uproject'),
           '-unattended', '-nosplash', '-nosound', '-NoLoadingScreen', '-NullRHI', '-abslog=' + str(out / 'Editor.log')]
if args.task in ['assets', 'exit']:
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction' / ('prepare_exit.py' if args.task == 'exit' else 'prepare_items.py'))]
else:
    test = {'persistence': 'Persistence', 'inventory': 'Inventory', 'recover-existing': 'RecoverExistingStore'}[args.task]
    if args.task == 'recover-existing':
        if not args.store or not (args.store / 'Store.journal').is_file():
            raise ValueError('An explicit existing journal is required')
        command += ['-ExtractionRecoveryStore=' + str(args.store.resolve())]
    command += ['-ExecCmds=Automation RunTests BrokenEdge.Extraction.' + test,
                '-TestExit=Automation Test Queue Empty', '-ReportExportPath=' + str(out / 'Automation')]
metadata = {'task': args.task, 'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'command': command}
environment = os.environ.copy()
environment['UE_SKIP_UBT_SDK_SETUP'] = '1'
with (out / 'Process.log').open('wb') as log:
    proc = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    metadata['pid'] = proc.pid
    (out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print('EDITOR_TASK_STARTED', proc.pid, str(out), flush=True)
    try:
        metadata['exit_code'] = proc.wait(timeout=600)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], capture_output=True)
        metadata['exit_code'] = proc.wait(timeout=30)
        metadata['error'] = 'Editor task timed out'
text = (out / 'Editor.log').read_text(encoding='utf-8', errors='replace')
passed = metadata['exit_code'] == 0 and 'Fatal error:' not in text and 'Traceback (most recent call last)' not in text
if args.task in ['assets', 'exit']:
    passed = passed and ('EXTRACTION_PHASE4_EXIT_PREPARED' if args.task == 'exit' else 'EXTRACTION_PHASE3_ASSETS_PREPARED') in text
else:
    successes = re.findall(r'Test Completed\. Result=\{(?:Success|\u6210\u529f)\}', text)
    metadata['successful_tests'] = len(successes)
    expected = {'persistence': 4, 'inventory': 3, 'recover-existing': 1}[args.task]
    passed = passed and len(successes) == expected and not re.search(r'Test Completed\. Result=\{(?:Fail|Failure|\u5931\u8d25)', text)
metadata['result'] = 'passed' if passed else 'failed'
metadata['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'result': metadata['result'], 'exit_code': metadata['exit_code'], 'successful_tests': metadata.get('successful_tests')}, ensure_ascii=True), flush=True)
if not passed:
    for line in text.splitlines():
        if 'Error:' in line or 'Traceback' in line or 'Exception' in line:
            print(line.encode('ascii', errors='backslashreplace').decode(), flush=True)
sys.exit(0 if passed else 1)
