"""Kill owned commandlet processes at durable boundaries, then verify twice in fresh processes."""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path('C:/UnrealProjects/UnrealEngine-5.8.3-release')
parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True)
args = parser.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
report = {'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'runs': [], 'result': 'running'}
env = dict(os.environ, UE_SKIP_UBT_SDK_SETUP='1')
try:
    for boundary in ['prepared', 'active', 'success', 'death']:
        folder = out / boundary
        folder.mkdir()
        for attempt in range(3):
            log = out / f'{boundary}-{attempt}.log'
            command = [str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'), str(ROOT / 'BrokenEdge.uproject'),
                       '-unattended', '-nosplash', '-nosound', '-NullRHI', '-NoLoadingScreen',
                       '-ExecCmds=Automation RunTests BrokenEdge.Extraction.CrashBoundary',
                       '-TestExit=Automation Test Queue Empty', '-abslog=' + str(log),
                       '-ExtractionBoundaryDir=' + str(folder), '-ExtractionBoundary=' + boundary]
            if attempt: command += ['-ExtractionVerifyRecovery']
            with (out / f'{boundary}-{attempt}-process.log').open('wb') as stream:
                proc = subprocess.Popen(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
                record = {'boundary': boundary, 'attempt': attempt, 'pid': proc.pid, 'command': command}
                report['runs'].append(record)
                try:
                    if attempt == 0:
                        deadline = time.monotonic() + 180
                        while not (folder / 'Ready.txt').exists():
                            if proc.poll() is not None or time.monotonic() > deadline:
                                raise RuntimeError(f'{boundary} did not reach durable boundary')
                            time.sleep(.25)
                        subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], capture_output=True, check=True)
                        record['intentional_crash'] = True
                    record['exit_code'] = proc.wait(timeout=180)
                    if attempt:
                        text = log.read_text(encoding='utf-8', errors='replace')
                        assert record['exit_code'] == 0 and ('Result={Success}' in text or 'Result={成功}' in text), str(log)
                        assert 'Result={Fail' not in text and 'Result={失败}' not in text, str(log)
                    print('PASS', boundary, attempt, flush=True)
                finally:
                    if proc.poll() is None:
                        subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], capture_output=True)
                        proc.wait(timeout=30)
    report['result'] = 'passed'
except Exception as error:
    report['result'] = 'failed'
    report['error'] = repr(error)
    raise
finally:
    report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
