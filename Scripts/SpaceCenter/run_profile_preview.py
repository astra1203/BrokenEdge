"""Serial, guarded preparation of a reviewable preview; never integrates production maps."""
import argparse
import datetime
import subprocess
import sys
from restoration_common import ROOT, WORK, PROFILE, PROFILE_FILE, read, sha, write_json

p=argparse.ArgumentParser()
p.add_argument('--tag',required=True)
p.add_argument('--manifest',required=True)
p.add_argument('--start-at')
p.add_argument('--capture-world',choices=['pie','editor'],default='pie')
args=p.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out=WORK/'PreviewRuns'/args.tag
out.mkdir(parents=True,exist_ok=False)
steps=[]
tasks=['verify','meshes']+(['collision-fidelity'] if PROFILE['phase'] in {'7E','7F'} else [])+['textures','materials','warm','preview','audit','capture']
for task in tasks:
    tag=args.tag+'-'+task
    common=['--tag',tag,'--profile',str(PROFILE_FILE)]
    if task=='verify':
        command=[sys.executable,str(ROOT/'Scripts/SpaceCenter/verify_restoration_inputs.py'),*common]
    elif task in ['textures','warm']:
        command=[sys.executable,str(ROOT/'Scripts/SpaceCenter/run_restoration_imports.py'),'--task',task,
                 '--manifest',args.manifest,*common]
    else:
        command=[sys.executable,str(ROOT/'Scripts/SpaceCenter/run_restoration_editor.py'),task,'--manifest',args.manifest,*common]
        if task=='capture':command+=['--capture-world',args.capture_world]
    if task not in ['textures','warm']:
        command=[sys.executable,str(ROOT/'Scripts/Extraction/resource_guard.py'),'--tag',tag,'--timeout','5400','--',*command]
    steps.append((task,command))
if args.start_at:steps=steps[[s[0] for s in steps].index(args.start_at):]
report={'result':'running','manifest_sha256':sha(args.manifest),'profile_sha256':sha(PROFILE_FILE),'steps':[],
        'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
try:
    for task,command in steps:
        entry={'name':task,'command':command};report['steps'].append(entry);write_json(out/'Report.json',report)
        print('PREVIEW_STAGE',task,flush=True)
        with (out/(task+'.log')).open('wb') as log:
            process=subprocess.Popen(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
            entry['pid']=process.pid;write_json(out/'Report.json',report);entry['exit_code']=process.wait()
        write_json(out/'Report.json',report)
        if entry['exit_code']:raise RuntimeError(task+' failed')
        print('PREVIEW_PASSED',task,flush=True)
    report['result']='passed'
except Exception as error:
    report.update(result='failed',error=repr(error));raise
finally:
    report['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();write_json(out/'Report.json',report)
