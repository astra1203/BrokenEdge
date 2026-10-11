"""One owned commandlet; resource_guard is the outer owner of its process tree."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('stage', choices=['animations', 'ladders', 'ladder-audit', 'source-route', 'route', 'visuals', 'geometry', 'obstacles'])
parser.add_argument('--tag', required=True)
args = parser.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out = ROOT/'Saved/Traversal/EditorRuns'/args.tag
out.mkdir(parents=True, exist_ok=False)
script = ROOT/'Scripts/Traversal'/dict(animations='import_animations.py',ladders='bind_ladders.py',**{'ladder-audit':'audit_source_ladders.py','source-route':'probe_source_route.py'},route='probe_route.py',visuals='configure_visuals.py',geometry='inspect_ladder_mesh.py',obstacles='probe_obstacles.py')[args.stage]
command = ['C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Binaries/Win64/UnrealEditor-Cmd.exe',
           str(ROOT/'BrokenEdge.uproject'), '-run=pythonscript', '-script='+str(script), '-NullRHI',
           '-unattended', '-nosplash', '-nosound', '-abslog='+str(out/'Editor.log')]
env = os.environ.copy()
env['TRAVERSAL_EDITOR_OUT'] = str(out)
meta = dict(stage=args.stage, command=command, script_sha256=hashlib.sha256(script.read_bytes()).hexdigest(),
            started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
with (out/'Process.log').open('wb') as log:
    p = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    meta['pid'] = p.pid
    (out/'Run.json').write_text(json.dumps(meta, indent=2), encoding='utf-8')
    meta['exit_code'] = p.wait()
report = json.loads((out/'Report.json').read_text(encoding='utf-8')) if (out/'Report.json').exists() else {}
meta['result'] = 'passed' if meta['exit_code'] == 0 and report.get('result') == 'passed' else 'failed'
(out/'Run.json').write_text(json.dumps(meta, indent=2)+'\n', encoding='utf-8')
print('TRAVERSAL_EDITOR', meta['result'], out, flush=True)
sys.exit(0 if meta['result'] == 'passed' else 1)
