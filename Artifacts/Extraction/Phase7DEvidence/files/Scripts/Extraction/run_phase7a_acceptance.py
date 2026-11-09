"""Serial, resource-guarded acceptance with immutable run directories."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('group', choices=['packages', 'rules', 'streaming', 'regression', 'performance', 'network', 'legacy'])
parser.add_argument('--tag', required=True)
parser.add_argument('--start-at')
args = parser.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
steps = []


def add(name, script, *options):
    tag = args.tag + '-' + name
    command = [sys.executable, str(ROOT / 'Scripts/Extraction/resource_guard.py'),
               '--tag', tag, '--timeout', '2400', '--', sys.executable,
               str(ROOT / 'Scripts/Extraction' / script), '--tag', tag, *options]
    steps.append((name, command))


if args.group == 'packages':
    for target in ['Server', 'Client']:
        add(target.lower(), 'run_build.py', '--target', target, '--package', '--jobs', '4')
elif args.group == 'rules':
    for task in ['phase7a-audit', 'inventory', 'persistence', 'equipment', 'progression']:
        add(task, 'run_editor_task.py', task)
elif args.group == 'streaming':
    add('two-rendered', 'run_network.py', '--scenario', 'streaming', '--players', '2',
        '--rendered-clients', '2', '--streaming-loops', '10', '--packaged')
    for lag in [False, True]:
        add('six' + ('-lag' if lag else ''), 'run_network.py', '--scenario', 'streaming',
            '--players', '6', '--rendered-clients', '1', '--streaming-loops', '2',
            '--packaged', *(['--lag'] if lag else []))
    for lag in [False, True]:
        add('north-combat' + ('-lag' if lag else ''), 'run_network.py', '--scenario', 'northcombat',
            '--players', '6', '--rendered-clients', '1', '--packaged', *(['--lag'] if lag else []))
elif args.group == 'regression':
    for scenario in ['progression', 'equipment', 'squad', 'pvpve', 'lifecycle']:
        add(scenario, 'run_network.py', '--scenario', scenario, '--players', '6',
            '--rendered-clients', '3' if scenario == 'squad' else '2' if scenario in ['equipment', 'lifecycle'] else '1', '--packaged', *(['--lag'] if scenario in ['progression', 'pvpve'] else []))
    for scenario in ['inventory', 'settlement', 'disconnect']:
        add(scenario, 'run_network.py', '--scenario', scenario, '--packaged')
elif args.group in ['performance', 'network']:
    for route in ['old', 'north']:
        add(route, 'run_network.py', '--scenario', 'performance', '--performance-route', route,
            '--players', '2', '--rendered-clients', '1', '--packaged',
            *(['--performance-samples', '1', '--performance-seconds', '60', '--network-stats'] if args.group == 'network' else []))
elif args.group == 'legacy':
    add('extraction', 'run_pie_lobby.py', '--players', '3')
    for mode in ['Teams', 'FreeForAll']:
        add(mode.lower(), 'run_pie_lobby.py', '--mode', mode)
    add('teardown', 'run_pie_teardown.py')
    add('lan', 'run_legacy_lan.py')
if args.start_at:
    steps = steps[[name for name, _ in steps].index(args.start_at):]
report = {'group': args.group, 'result': 'running', 'steps': [],
          'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}


def save():
    (out / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


try:
    for name, command in steps:
        entry = {'name': name, 'command': command}
        report['steps'].append(entry)
        save()
        print('STEP', name, flush=True)
        with (out / (name + '.log')).open('wb') as log:
            process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            entry['pid'] = process.pid
            save()
            entry['exit_code'] = process.wait()
        save()
        if entry['exit_code']:
            raise RuntimeError(name + ' failed')
        print('PASS', name, flush=True)
    report['result'] = 'passed'
except Exception as error:
    report.update(result='failed', error=repr(error))
    raise
finally:
    report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save()
