"""Archive verified evidence, assets and optional sampled motion clips without source ZIP payloads."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
work=root/'Saved/Stinger/Locomotion'
p=argparse.ArgumentParser()
p.add_argument('--single',required=True);p.add_argument('--multi',required=True)
p.add_argument('--grip',required=True);p.add_argument('--actions',required=True);p.add_argument('--route',required=True)
p.add_argument('--weapon',required=True)
a=p.parse_args()
dest=root/'Artifacts/Stinger/LocomotionEvidence';dest.mkdir(parents=True,exist_ok=True)
summary={'scope':'Stinger AK opt-in; editor cold load and same-process PIE; no packaged or dedicated-server validation',
         'suites':[],'files':[],'assets':[],'videos':[]}

def copy(src,relative):
    target=dest/relative;target.parent.mkdir(parents=True,exist_ok=True)
    data=src.read_bytes()
    target.write_bytes(gzip.compress(data,mtime=0) if str(target).endswith('.gz') else data)
    summary['files'].append({'path':relative,'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'bytes':target.stat().st_size})

for filename in ('Sources.json','Import.json','BindingsBefore.json','Configuration.json','Audit.json'):
    copy(work/filename,filename)
copy(work/'AuditFinal.log','Build/ColdAudit.txt.gz')
copy(Path('C:/UnrealProjects/UnrealEngine-5.8.3-release/Engine/Programs/UnrealBuildTool/Log.txt'),'Build/UnrealBuildTool.txt.gz')
audit=json.loads((work/'Audit.json').read_text(encoding='utf-8'))
assert audit['result']=='passed'
summary['suites'].append({'name':'cold-audit','checks':len(audit['checks']),'result':'passed'})
copy(work/'export/ExportResults.json','Conversion/ExportResults.json')
copy(work/'inspect/Packages.json','Conversion/Packages.json.gz')
sources=[('single',work/'Runs'/a.single,'Report.json'),('multi',work/'Runs'/a.multi,'Report.json'),
         ('grip',root/'Saved/Stinger/Baseline'/a.grip,'third_person_grip_p0_2p.json'),
         ('actions',root/'Saved/Stinger/Baseline'/a.actions,'pie_validation.json'),
         ('route',root/'Saved/SpaceCenter/Runs'/a.route,'Report.json')]
for label,folder,filename in sources:
    report=json.loads((folder/filename).read_text(encoding='utf-8'))
    run=json.loads((folder/'Run.json').read_text(encoding='utf-8'))
    assert report['result']=='passed' and run['exit_code']==0,(label,report.get('error'))
    summary['suites'].append({'name':label,'run':folder.name,'checks':len(report['checks']),'result':'passed'})
    copy(folder/filename,label+'/Report.json.gz')
    copy(folder/'Run.json',label+'/Run.json')
    copy(folder/'Editor.log',label+'/Editor.txt.gz')

weapon=root/'Saved/Extraction'/a.weapon
run=json.loads((weapon/'Run.json').read_text(encoding='utf-8'))
assert run['result']=='passed' and run['exit_code']==0
summary['suites'].append({'name':'weapon-input','run':a.weapon,'tests':run['successful_tests'],'result':'passed'})
copy(weapon/'Run.json','weapon/Run.json')
copy(weapon/'Editor.log','weapon/Editor.txt.gz')

multi=work/'Runs'/a.multi/'Captures'
for name in ('walk_F','run_R','crouch_F','view_walk','view_run','view_sprint','view_aim'):
    copy(multi/(name+'.png'),'Captures/'+name+'.png')
sys.path.insert(0,str(root/'Saved/Traversal/Tools'))
import imageio_ffmpeg
for phase in ('walk_F','run_R','crouch_F','view_walk','view_run','view_sprint'):
    frames=sorted(multi.glob(phase+'_*.png'))
    if not frames:continue
    concat=work/(phase+'-concat.txt')
    concat.write_text('\n'.join("file '"+f.as_posix()+"'\nduration 0.166666667" for f in frames)+"\nfile '"+frames[-1].as_posix()+"'\n",encoding='utf-8')
    output=dest/'Captures'/(phase+'.mp4')
    subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-loglevel','error','-y','-f','concat','-safe','0','-i',str(concat),
                    '-fps_mode','vfr','-c:v','libx264','-threads','2','-crf','18','-pix_fmt','yuv420p',str(output)],check=True)
    subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-i',str(output),'-f','null','-'],check=True)
    summary['videos'].append({'path':str(output.relative_to(dest)),'frames':len(frames),'sample_fps':6,
        'boundary':'Sampled viewport capture at fixed simulation intervals; no audio or performance claim',
        'sha256':hashlib.sha256(output.read_bytes()).hexdigest()})
for path in sorted((root/'Content/Characters/Stinger/Animations/DeltaForceLocomotion').rglob('*.uasset')):
    summary['assets'].append({'path':path.relative_to(root).as_posix(),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
for name in ('Content/Blueprints/Weapon/BP_AK47.uasset','Content/Characters/Stinger/Blueprints/BP_StingerCharacter.uasset','Content/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired.uasset'):
    path=root/name
    summary['assets'].append({'path':name,'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
summary['implementation'] = []
for parent in ('Scripts/Stinger','Source/BrokenEdge/Animation','Source/BrokenEdge/Character','Source/StingerTools'):
    for path in sorted((root/parent).rglob('*')):
        if path.suffix in ('.py','.ps1','.h','.cpp'):
            summary['implementation'].append({'path':path.relative_to(root).as_posix(),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
for name in ('Scripts/SpaceCenter/Converter/Program.cs','Source/BrokenEdge/Weapon/WeaponPresentation.h'):
    summary['implementation'].append({'path':name,'sha256':hashlib.sha256((root/name).read_bytes()).hexdigest()})
summary['cue4parse_commit']=json.loads((root/'Scripts/SpaceCenter/sample_config.json').read_text(encoding='utf-8'))['cue_commit']
(dest/'Summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(summary['suites']))
