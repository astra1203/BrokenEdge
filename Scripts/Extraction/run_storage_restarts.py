"""Kill owned commandlet processes at durable boundaries, then verify twice in fresh processes."""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Common.ue_env import engine_root, run_record
parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True)
parser.add_argument('--engine', type=Path)
parser.add_argument('--economy', action='store_true', help='Buy, sell, file delivery, reward and settlement before/after commit')
args = parser.parse_args()
try:
    ENGINE = engine_root(args.engine)
except (ValueError, OSError, KeyError) as error:
    parser.error(str(error))
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
report = {'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'runs': [], 'result': 'running'}
report.update(run_record([sys.executable, __file__, *sys.argv[1:]], out / 'Report.json', engine=ENGINE, script=__file__))
(out / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
env = dict(os.environ, UE_SKIP_UBT_SDK_SETUP='1')
try:
    boundaries = [kind + '-' + point for kind in ['buy', 'sell', 'deliver', 'claim', 'settlement']
                  for point in ['before', 'after']] if args.economy else ['prepared', 'active', 'success', 'death']
    for boundary in boundaries:
        folder = out / boundary
        folder.mkdir()
        for attempt in range(3):
            log = out / f'{boundary}-{attempt}.log'
            command = [str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'), str(ROOT / 'BrokenEdge.uproject'),
                       '-unattended', '-nosplash', '-nosound', '-NullRHI', '-NoLoadingScreen',
                       '-ExecCmds=Automation RunTests BrokenEdge.Extraction.' + ('EconomyCrashBoundary' if args.economy else 'CrashBoundary'),
                       '-TestExit=Automation Test Queue Empty', '-abslog=' + str(log),
                       '-ExtractionBoundaryDir=' + str(folder), '-ExtractionBoundary=' + boundary]
            if attempt: command += ['-ExtractionVerifyRecovery', '-ExtractionRecoveryAttempt=' + str(attempt)]
            with (out / f'{boundary}-{attempt}-process.log').open('wb') as stream:
                proc = subprocess.Popen(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
                record = {'boundary': boundary, 'attempt': attempt, 'pid': proc.pid, 'command': command}
                report['runs'].append(record)
                (out / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
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
    report['exit_code'] = 0 if report['result'] == 'passed' else 1
    report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
