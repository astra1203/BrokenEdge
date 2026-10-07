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
parser.add_argument('task', choices=['assets', 'exit', 'inventory', 'persistence', 'recover-existing', 'phase5-assets', 'phase6a-assets', 'optic-assets', 'equipment', 'phase6b-assets', 'progression', 'north-import', 'phase7a-assets', 'phase7a-audit'])
parser.add_argument('--tag', required=True)
parser.add_argument('--store', type=Path, help='Existing store directory; only for recover-existing')
parser.add_argument('--verify-profile')
parser.add_argument('--verify-item')
args = parser.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
command = [str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'), str(ROOT / 'BrokenEdge.uproject'),
           '-unattended', '-nosplash', '-nosound', '-NoLoadingScreen', '-NullRHI', '-abslog=' + str(out / 'Editor.log')]
if args.task in ['north-import', 'phase7a-assets', 'phase7a-audit']:
    script = {'north-import': 'Scripts/SpaceCenter/import_sample.py', 'phase7a-assets': 'Scripts/Extraction/prepare_phase7a.py', 'phase7a-audit': 'Scripts/Extraction/audit_phase7a.py'}[args.task]
    command += ['-run=pythonscript', '-script=' + str(ROOT / script)]
elif args.task == 'phase6b-assets':
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/prepare_phase6b.py')]
elif args.task == 'optic-assets':
    script = ROOT / 'Scripts/Extraction/calibrate_red_dot.py'
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    str(ROOT / 'Scripts/Assert-EditorClosed.ps1'), '-ProjectFile', str(ROOT / 'BrokenEdge.uproject')], check=True)
    command += ['-run=pythonscript', '-script=' + str(script)]
elif args.task == 'phase6a-assets':
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/prepare_phase6a.py')]
elif args.task in ['assets', 'exit', 'phase5-assets']:
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction' / ('prepare_phase5.py' if args.task == 'phase5-assets' else 'prepare_exit.py' if args.task == 'exit' else 'prepare_items.py'))]
else:
    test = {'persistence': 'Persistence', 'inventory': 'Inventory', 'recover-existing': 'RecoverExistingStore', 'equipment': 'Equipment', 'progression': 'Progression'}[args.task]
    if args.task == 'recover-existing':
        if not args.store or not (args.store / 'Store.journal').is_file():
            raise ValueError('An explicit existing journal is required')
        command += ['-ExtractionRecoveryStore=' + str(args.store.resolve())]
        if args.verify_profile:
            if not args.verify_item: raise ValueError('Expected AI item is required with profile')
            command += ['-ExtractionVerifyProfile=' + args.verify_profile, '-ExtractionVerifyItem=' + args.verify_item]
    command += ['-ExecCmds=Automation RunTests BrokenEdge.Extraction.' + test,
                '-TestExit=Automation Test Queue Empty', '-ReportExportPath=' + str(out / 'Automation')]
metadata = {'task': args.task, 'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'command': command}
environment = os.environ.copy()
environment['UE_SKIP_UBT_SDK_SETUP'] = '1'
environment['EXTRACTION_RUN_DIR'] = str(out)
if args.task == 'north-import':
    environment.update(SPACECENTER_WORK=str(ROOT / 'Saved/SpaceCenter/Phase7A'), SPACECENTER_ASSETS_ONLY='1')
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
if args.task in ['north-import', 'phase7a-assets', 'phase7a-audit']:
    marker = {'north-import': 'SPACE_REGION_ASSETS_COMPLETE', 'phase7a-assets': 'EXTRACTION_PHASE7A_ASSETS_PREPARED', 'phase7a-audit': 'EXTRACTION_PHASE7A_AUDIT_PASSED'}[args.task]
    passed = passed and marker in text
elif args.task == 'phase6b-assets':
    passed = passed and 'EXTRACTION_PHASE6B_ASSETS_PREPARED' in text
elif args.task == 'optic-assets':
    passed = passed and 'EXTRACTION_OPTIC_PREPARED' in text
elif args.task == 'phase6a-assets':
    passed = passed and 'EXTRACTION_PHASE6A_ASSETS_PREPARED' in text
elif args.task in ['assets', 'exit', 'phase5-assets']:
    passed = passed and ('EXTRACTION_PHASE5_ASSETS_PREPARED' if args.task == 'phase5-assets' else 'EXTRACTION_PHASE4_EXIT_PREPARED' if args.task == 'exit' else 'EXTRACTION_PHASE3_ASSETS_PREPARED') in text
else:
    successes = re.findall(r'Test Completed\. Result=\{(?:Success|\u6210\u529f)\}', text)
    metadata['successful_tests'] = len(successes)
    expected = {'persistence': 4, 'inventory': 3, 'recover-existing': 1, 'equipment': 4, 'progression': 4}[args.task]
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
