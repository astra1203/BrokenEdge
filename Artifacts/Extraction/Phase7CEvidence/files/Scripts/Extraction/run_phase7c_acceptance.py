"""Fresh reconstruction acceptance, with one rendered streaming client at a time."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('group',choices=['packages','rules','streaming','regression','performance'])
p.add_argument('--tag',required=True);p.add_argument('--start-at');args=p.parse_args()
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out=ROOT/'Saved/Extraction'/args.tag;out.mkdir(parents=True,exist_ok=False)
steps=[]


def add(name,script,*options,timeout=2400):
    tag=args.tag+'-'+name
    steps.append((name,[sys.executable,str(ROOT/'Scripts/Extraction/resource_guard.py'),'--tag',tag,'--timeout',str(timeout),'--',
                       sys.executable,str(ROOT/'Scripts/Extraction'/script),'--tag',tag,*options]))


if args.group=='packages':
    assert (ROOT/'Scripts/SpaceCenter/restoration_active.json').is_file(),'Integrate the reviewed preview before packaging'
    for target in ['Server','Client']:add(target.lower(),'run_build.py','--target',target,'--package','--jobs','1','--cook-workers','4',timeout=7200)
elif args.group=='rules':
    for task in ['inventory','persistence','equipment','progression']:add(task,'run_editor_task.py',task)
elif args.group=='streaming':
    for role in ['A','B']:
        add('two-rendered-'+role.lower(),'run_network.py','--scenario','streaming','--players','2','--rendered-role',role,
            '--streaming-loops','10','--resolution','1920x1080','--packaged')
    for lag in [False,True]:
        options=['--lag'] if lag else []
        for scenario in ['streaming','northcombat']:
            add('six-'+scenario+('-lag' if lag else ''),'run_network.py','--scenario',scenario,'--players','6',
                '--rendered-clients','1','--streaming-loops','2','--resolution','1920x1080','--packaged',*options)
elif args.group=='regression':
    for scenario in ['progression','equipment','squad','pvpve','lifecycle']:
        rendered='3' if scenario=='squad' else '2' if scenario in ['equipment','lifecycle'] else '1'
        add(scenario,'run_network.py','--scenario',scenario,'--players','6','--rendered-clients',rendered,'--packaged',
            *(['--lag'] if scenario in ['progression','pvpve'] else []))
    for scenario in ['inventory','settlement','disconnect']:add(scenario,'run_network.py','--scenario',scenario,'--packaged')
    add('optic','run_network.py','--scenario','equipment','--optic-validation','--players','2','--rendered-role','B',
        '--resolution','1920x1080','--packaged')
elif args.group=='performance':
    for route in ['old','north']:
        add(route,'run_network.py','--scenario','performance','--performance-route',route,'--performance-samples','3',
            '--performance-seconds','180','--players','2','--rendered-clients','1','--resolution','1920x1080','--packaged')
if args.start_at:steps=steps[[s[0] for s in steps].index(args.start_at):]
report={'group':args.group,'result':'running','steps':[],'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}


def save():(out/'Report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


try:
    for name,command in steps:
        entry={'name':name,'command':command};report['steps'].append(entry);save()
        print('PHASE7C_STEP',name,flush=True)
        with (out/(name+'.log')).open('wb') as log:
            process=subprocess.Popen(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
            entry['pid']=process.pid;save();entry['exit_code']=process.wait()
        save()
        if entry['exit_code']:raise RuntimeError(name+' failed')
        print('PHASE7C_PASS',name,flush=True)
    report['result']='passed'
except Exception as error:
    report.update(result='failed',error=repr(error));raise
finally:
    report['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
