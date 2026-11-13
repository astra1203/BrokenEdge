"""Run an isolated Stinger baseline suite without overwriting historical reports."""
import argparse
import datetime
import json
import os
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
from Common.ue_env import engine_root, run_record
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('suite', choices=['audit', 'grip', 'visibility', 'actions', 'combat'])
parser.add_argument('--players', type=int, choices=[1, 2], default=2)
parser.add_argument('--capture', choices=['0', 'stills', 'video'], default='0')
parser.add_argument('--tag', required=True)
parser.add_argument('--engine', type=Path)
args = parser.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag), 'Tag must be a directory name.'
project = Path(__file__).resolve().parents[2]
try:
    engine = engine_root(args.engine)
except (ValueError, OSError) as error:
    parser.error(str(error))
out = project / 'Saved/Stinger/Baseline' / args.tag
out.mkdir(parents=True, exist_ok=False)
scripts = {'audit': 'audit_baseline.py', 'grip': 'validate_third_person_grip.py',
           'visibility': 'validate_weapon_visibility.py', 'actions': 'validate_pie.py',
           'combat': 'validate_baseline_combat.py'}
script = project / 'Scripts/Stinger' / scripts[args.suite]
assert script.is_file(), script
env = os.environ.copy()
env['STINGER_BASELINE_DIR'] = str(out)
env['STINGER_GRIP_PLAYERS'] = env['STINGER_VISIBILITY_PLAYERS'] = str(args.players)
env['STINGER_GRIP_STAGE'] = 'p0'
env['STINGER_GRIP_CAPTURE'] = env['STINGER_VISIBILITY_CAPTURE'] = args.capture
env['STINGER_ISOLATE_INPUT'] = '1'
env['STINGER_CHARACTER_NAME'] = 'BP_StingerCharacter_C'
env.pop('STINGER_VISUAL_CAPTURE', None)
exe = engine / 'Engine/Binaries/Win64' / ('UnrealEditor-Cmd.exe' if args.suite == 'audit' else 'UnrealEditor.exe')
command = [str(exe), str(project / 'BrokenEdge.uproject')]
if args.suite == 'audit':
    command += ['-run=pythonscript', '-script=' + str(script), '-NullRHI']
else:
    command += ['/Game/Characters/Stinger/Maps/L_StingerTest', '-RenderOffscreen',
                '-windowed', '-ResX=1280', '-ResY=720', '-ExecCmds=py ' + str(script)]
command += ['-unattended', '-nosplash', '-nosound', '-NoLoadingScreen', '-abslog=' + str(out / 'Editor.log')]
metadata = {'suite': args.suite, 'players': args.players, 'capture': args.capture,
            'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=project, text=True).strip(),
            'worktree': subprocess.check_output(['git', 'status', '--short'], cwd=project, text=True),
            'command': command}
metadata.update(run_record(command, out / 'Run.json', engine=engine, script=__file__))
print('BASELINE_OUTPUT', out, flush=True)
with (out / 'Process.log').open('w', encoding='utf-8') as log:
    process = subprocess.Popen(command, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    metadata['pid'] = process.pid
    print('BASELINE_PID', process.pid, flush=True)
    try:
        metadata['exit_code'] = process.wait(timeout=1000)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
        metadata['error'] = 'Editor suite timed out'
        metadata['exit_code'] = process.wait(timeout=30)
metadata['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
names = {'audit': 'Audit.json', 'grip': f'third_person_grip_p0_{args.players}p.json',
         'visibility': f'weapon_visibility_{args.players}p' + ('_stills' if args.capture == 'stills' else '') + '.json',
         'actions': 'pie_validation.json', 'combat': 'Combat.json'}
report_path = out / names[args.suite]
if not report_path.exists():
    print('BASELINE_FAILED: report missing; inspect', out / 'Editor.log')
    sys.exit(1)
report = json.loads(report_path.read_text(encoding='utf-8-sig'))
failed = [check for check in report.get('checks', []) if not check['passed']]
print(json.dumps({'result': report.get('result'), 'exit_code': metadata['exit_code'],
                  'checks': len(report.get('checks', [])), 'failures': failed,
                  'error': report.get('error')}, ensure_ascii=True), flush=True)
# A successful report and a successful process are both required.
sys.exit(0 if report.get('result') == 'passed' and metadata['exit_code'] == 0 and 'error' not in metadata else 1)
