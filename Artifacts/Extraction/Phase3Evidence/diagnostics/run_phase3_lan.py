import datetime
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
out = root / 'Saved/Extraction/Phase3LegacyLAN01'
out.mkdir(exist_ok=False)
before = set((root / 'Saved/SessionTests').glob('Dedicated-*'))
command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
           str(root / 'Scripts/Test-LanSessions.ps1'), '-Packaged']
report = {'command': command, 'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'result': 'running', 'checks': []}
with (out / 'Console.log').open('wb') as output:
    process = subprocess.Popen(command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    report['pid'] = process.pid
    print('LEGACY_LAN_STARTED', process.pid, str(out), flush=True)
    report['exit_code'] = process.wait()
folders = set((root / 'Saved/SessionTests').glob('Dedicated-*')) - before
assert len(folders) == 1, folders
folder = folders.pop()
report['original_logs'] = str(folder.relative_to(root))
for log in folder.glob('*.log'):
    shutil.copyfile(log, out / log.name)
    if log.name.startswith('Other-'):
        continue
    content = log.read_text(encoding='utf-8', errors='replace')
    success = bool(re.search(r'Test Completed\. Result=\{(?:Success|\u6210\u529f)\}', content))
    success &= not bool(re.search(r'Fatal error:|Test Completed\. Result=\{(?:Fail|Failed|\u5931\u8d25)', content))
    report['checks'].append({'name': log.stem, 'passed': success})
report['result'] = 'passed' if report['exit_code'] == 0 and len(report['checks']) == 7 and all(c['passed'] for c in report['checks']) else 'failed'
report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(out / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('LEGACY_LAN', report['result'], len(report['checks']), flush=True)
sys.exit(0 if report['result'] == 'passed' else 1)
