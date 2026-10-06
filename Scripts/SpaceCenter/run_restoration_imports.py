"""Serialize bounded editor import batches with a fresh resource preflight each time."""
import argparse
import subprocess
import sys
from restoration_common import *

p=argparse.ArgumentParser();p.add_argument('--tag',required=True);p.add_argument('--manifest',required=True)
p.add_argument('--task',choices=['textures','warm'],default='textures')
p.add_argument('--limit',type=int,default=250);args=p.parse_args()
if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):p.error('Use a new alphanumeric tag')
out=WORK/'ImportRuns'/args.tag;out.mkdir(parents=True,exist_ok=False)
report={'result':'running','batches':[]};write_json(out/'Report.json',report)
for index in range(1,65):
    tag=args.tag+'-%02d'%index
    command=[sys.executable,str(ROOT/'Scripts/Extraction/resource_guard.py'),'--tag',tag,'--timeout','3600','--',
             sys.executable,str(ROOT/'Scripts/SpaceCenter/run_restoration_editor.py'),args.task,'--tag',tag,
             '--manifest',str(Path(args.manifest).resolve()),'--limit',str(args.limit)]
    result=subprocess.run(command,cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    detail=read(WORK/'EditorRuns'/tag/'Report.json') if (WORK/'EditorRuns'/tag/'Report.json').exists() else {}
    report['batches'].append({'tag':tag,'exit_code':result.returncode,'remaining':detail.get('remaining')})
    if result.returncode:
        report['result']='failed';write_json(out/'Report.json',report);sys.exit(result.returncode)
    write_json(out/'Report.json',report)
    print('RESTORATION_IMPORT_BATCH',index,'REMAINING',detail.get('remaining',0),flush=True)
    if not detail.get('remaining'):
        report['result']='passed';write_json(out/'Report.json',report);break
else:raise RuntimeError('Import did not converge within bounded batch count')
