"""Run the locomotion suite in an isolated offscreen editor with a unique evidence directory."""
import argparse,json,os,subprocess,sys
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
from Common.ue_env import engine_root, run_record
p=argparse.ArgumentParser();p.add_argument('--players',type=int,choices=[1,2],default=1);p.add_argument('--tag',required=True);p.add_argument('--video',action='store_true')
p.add_argument('--engine', type=Path)
a=p.parse_args()
try:
    engine=engine_root(a.engine)
except (ValueError, OSError) as error:
    p.error(str(error))
assert a.tag and all(c.isalnum() or c in '-_' for c in a.tag)
root=Path(__file__).resolve().parents[2];out=root/'Saved/Stinger/Locomotion/Runs'/a.tag
out.mkdir(parents=True,exist_ok=False)
env=os.environ.copy();env.update(LOCOMOTION_RUN=str(out),LOCOMOTION_PLAYERS=str(a.players))
env['LOCOMOTION_VIDEO']='1' if a.video else '0'
cmd=[str(engine/'Engine/Binaries/Win64/UnrealEditor.exe'),str(root/'BrokenEdge.uproject'),
     '/Game/Characters/Stinger/Maps/L_StingerTest','-RenderOffscreen','-windowed','-ResX=1280','-ResY=720',
     '-ExecCmds=py '+str(Path(__file__).with_name('validate_locomotion.py').resolve()),'-unattended','-nosplash','-nosound','-abslog='+str(out/'Editor.log')]
metadata=run_record(cmd, out/'Run.json', engine=engine, script=__file__)
metadata['players']=a.players
(out/'Run.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
with (out/'Process.log').open('w',encoding='utf-8') as log:
    result=subprocess.run(cmd,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=700,creationflags=subprocess.CREATE_NO_WINDOW)
metadata['exit_code']=result.returncode
(out/'Run.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
report=json.loads((out/'Report.json').read_text(encoding='utf-8'))
print(json.dumps({'result':report['result'],'error':report.get('error'),'failures':[x for x in report['checks'] if not x['passed']]},ensure_ascii=True))
sys.exit(0 if result.returncode==0 and report['result']=='passed' else 1)
