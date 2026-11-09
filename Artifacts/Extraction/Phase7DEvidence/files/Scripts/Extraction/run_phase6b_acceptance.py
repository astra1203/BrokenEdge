"""Sequential Phase 6B acceptance groups with owned children and immutable run tags."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('group', choices=['packages', 'network', 'storage', 'pie'])
parser.add_argument('--tag', required=True)
parser.add_argument('--start-at', help='Resume a group at the named step using a new tag')
args = parser.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
steps = []

def add(name, script, *options):
    steps.append((name, [sys.executable, str(ROOT / 'Scripts/Extraction' / script),
                        '--tag', args.tag + '-' + name, *options]))

if args.group == 'packages':
    for target in ['Server', 'Client']:
        add(target.lower(), 'run_build.py', '--target', target, '--package', '--jobs', '6')
elif args.group == 'network':
    for scenario in ['progression', 'equipment', 'squad', 'pvpve']:
        for lag in [False, True]:
            add(scenario + ('-lag' if lag else ''), 'run_network.py', '--scenario', scenario,
                '--players', '6', '--packaged', *(['--lag'] if lag else []))
    for scenario in ['lifecycle', 'inventory', 'combat', 'spectator', 'four', 'settlement', 'recovery']:
        add(scenario, 'run_network.py', '--scenario', scenario, '--packaged',
            *(['--unacked'] if scenario == 'recovery' else []))
elif args.group == 'storage':
    add('economy-crash', 'run_storage_restarts.py', '--economy')
    add('raid-crash', 'run_storage_restarts.py')
elif args.group == 'pie':
    add('extraction', 'run_pie_lobby.py', '--players', '3')
    for mode in ['Teams', 'FreeForAll']:
        add(mode.lower(), 'run_pie_lobby.py', '--mode', mode)
    add('teardown', 'run_pie_teardown.py')

if args.start_at:
    names = [name for name, _ in steps]
    if args.start_at not in names:
        raise ValueError('Unknown step: ' + args.start_at)
    steps = steps[names.index(args.start_at):]

report = {'group': args.group, 'start_at': args.start_at, 'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'result': 'running', 'steps': []}
def save():
    (out / 'Report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
try:
    for name, command in steps:
        entry = {'name': name, 'command': command}
        report['steps'].append(entry)
        save()
        print('STEP', name, flush=True)
        with (out / (name + '.log')).open('wb') as log:
            child = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            entry['pid'] = child.pid
            save()
            entry['exit_code'] = child.wait()
        save()
        if entry['exit_code']:
            raise RuntimeError(name + ' failed; inspect ' + str(out / (name + '.log')))
        print('PASS', name, flush=True)
    report['result'] = 'passed'
except Exception as error:
    report['result'] = 'failed'
    report['error'] = repr(error)
    raise
finally:
    report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save()
