"""Run an isolated editor with tagged, non-overwriting process evidence."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
from Common.ue_env import engine_root, run_record
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('suite', choices=['import', 'audit', 'capture', 'traverse'])
parser.add_argument('--tag', required=True)
parser.add_argument('--players', type=int, choices=[1, 2], default=1)
parser.add_argument('--engine', type=Path)
args = parser.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
project = Path(__file__).resolve().parents[2]
try:
    engine = engine_root(args.engine)
except (ValueError, OSError) as error:
    parser.error(str(error))
out = project / 'Saved/SpaceCenter/Runs' / args.tag
out.mkdir(parents=True, exist_ok=False)
script = Path(__file__).parent / ({'import': 'import_sample.py', 'audit': 'audit_sample.py'}.get(args.suite, 'validate_sample.py'))
rendered = args.suite in ['capture', 'traverse']
exe = engine / 'Engine/Binaries/Win64' / ('UnrealEditor.exe' if rendered else 'UnrealEditor-Cmd.exe')
command = [str(exe), str(project / 'BrokenEdge.uproject')]
if rendered:
    command += ['/Game/Maps/SpaceCenter/L_SpaceCenterSample', '-RenderOffscreen', '-windowed', '-ResX=1280', '-ResY=720', '-csvGpuStats', '-csvCategories=GPUMem', '-ExecCmds=py ' + str(script)]
else:
    command += ['-run=pythonscript', '-script=' + str(script), '-NullRHI']
command += ['-unattended', '-nosplash', '-nosound', '-abslog=' + str(out / 'Editor.log')]
env = os.environ.copy()
env.update(SPACECENTER_RUN_DIR=str(out), SPACECENTER_SUITE=args.suite, SPACECENTER_PLAYERS=str(args.players))
metadata = {'command': command, 'suite': args.suite, 'players': args.players,
            'input_sha256': {str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest() for p in Path(__file__).parent.glob('*') if p.is_file()},
            'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=project, text=True).strip(),
            'worktree': subprocess.check_output(['git', 'status', '--short'], cwd=project, text=True)}
metadata.update(run_record(command, out / 'Run.json', engine=engine, script=__file__))
with (out / 'Process.log').open('w', encoding='utf-8') as log:
    process = subprocess.Popen(command, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    metadata['pid'] = process.pid
    (out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print('SPACE_RUN', out, 'PID', process.pid, flush=True)
    try:
        metadata['exit_code'] = process.wait(timeout=1800)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
        metadata['error'] = 'Isolated editor timed out'
        metadata['exit_code'] = process.wait(timeout=30)
metadata['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
report_path = out / 'Report.json'
if args.suite == 'import':
    source = project / 'Saved/SpaceCenter/ImportReport.json'
    if source.exists():
        report_path.write_bytes(source.read_bytes())
passed = report_path.exists() and json.loads(report_path.read_text(encoding='utf-8')).get('result') == 'passed'
print('SPACE_EXIT', metadata['exit_code'], 'REPORT_PASS', passed, flush=True)
sys.exit(0 if passed and metadata['exit_code'] == 0 and 'error' not in metadata else 1)
