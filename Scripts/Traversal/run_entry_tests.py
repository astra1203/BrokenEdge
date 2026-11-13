"""Recorded native capsule/entry tests; run under resource_guard after an Editor build."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
from Common.ue_env import engine_root, run_record
import re
import subprocess
import argparse

parser=argparse.ArgumentParser()
parser.add_argument('--tag',required=True)
parser.add_argument('--engine', type=Path)
args=parser.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
root=Path(__file__).resolve().parents[2]
try:
    engine = engine_root(args.engine)
except (ValueError, OSError) as error:
    parser.error(str(error))
out=root/'Saved/Traversal/EditorRuns'/args.tag
out.mkdir(parents=True,exist_ok=False)
command=[str(engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'),
         str(root/'BrokenEdge.uproject'),'/Game/Maps/Lobby','-unattended','-NullRHI','-nosound','-nosplash',
         '-ExecCmds=Automation RunTests BrokenEdge.Traversal.EntryCapsule','-TestExit=Automation Test Queue Empty',
         '-ReportExportPath='+str(out/'Automation'),'-abslog='+str(out/'Editor.log')]
report=dict(result='running',started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),command=command,
            inputs={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in (
                'Source/BrokenEdge/Tests/TraversalEntryTests.cpp','Source/BrokenEdge/Traversal/ExtractionLadder.cpp',
                'Source/BrokenEdge/Traversal/TraversalLadderEntry.h')})
report.update(run_record(command, out / 'Run.json', engine=engine, script=__file__))
def save():(out/'Run.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
with (out/'Process.log').open('wb') as log:
    process=subprocess.Popen(command,cwd=root,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    report['pid']=process.pid;save()
    report['exit_code']=process.wait()
log=(out/'Editor.log').read_text(encoding='utf-8',errors='replace')
report['successful_tests']=len(re.findall(r'Test Completed\. Result=\{(?:Success|\u6210\u529f)\}',log))
report['result']='passed' if report['exit_code']==0 and report['successful_tests']==1 and 'Fatal error:' not in log else 'failed'
save()
print('ENTRY_TESTS',report['result'],out,flush=True)
raise SystemExit(0 if report['result']=='passed' else 1)
