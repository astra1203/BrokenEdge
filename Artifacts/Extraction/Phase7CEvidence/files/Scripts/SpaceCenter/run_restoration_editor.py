"""Run one owned editor stage with immutable logs and explicit source inputs."""
import argparse
import datetime
import os
import subprocess
import sys
from restoration_common import ROOT, WORK, read, sha, write_json

parser=argparse.ArgumentParser()
parser.add_argument('task',choices=['meshes','textures','materials','terrain-fidelity','warm','preview','capture','capture-integrated','traverse','audit','audit-integrated','integrate'])
parser.add_argument('--tag',required=True)
parser.add_argument('--manifest',help='Explicit source manifest; defaults to the active reconstruction marker')
parser.add_argument('--limit',type=int,default=0)
args=parser.parse_args()
if not args.manifest:
    marker=ROOT/'Scripts/SpaceCenter/restoration_active.json'
    if not marker.is_file():parser.error('Pass --manifest for the initial reconstruction')
    args.manifest=str(ROOT/read(marker)['manifest_file'])
if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):parser.error('Use a new tag')
out=WORK/'EditorRuns'/args.tag
out.mkdir(parents=True,exist_ok=False)
subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'Scripts/Assert-EditorClosed.ps1'),
                '-ProjectFile',str(ROOT/'BrokenEdge.uproject')],check=True)
config=read(ROOT/'Scripts/SpaceCenter/north_config.json')
from pathlib import Path
engine=Path(config['engine']);rendered=args.task in ['capture','capture-integrated','traverse']
script=ROOT/'Scripts/SpaceCenter'/({'capture':'capture_restoration.py','capture-integrated':'capture_restoration.py','traverse':'validate_sample.py'}.get(args.task,'import_restoration.py'))
command=[str(engine/'Engine/Binaries/Win64'/('UnrealEditor.exe' if rendered else 'UnrealEditor-Cmd.exe')),str(ROOT/'BrokenEdge.uproject')]
if rendered:
    map_name='L_SpaceCenterExtraction' if args.task=='capture-integrated' else 'L_SpaceCenterRestorationPreview'
    command+=['/Game/Maps/SpaceCenter/'+map_name,'-RenderOffscreen','-windowed','-ResX=1920','-ResY=1080','-ForceRes','-ExecCmds=py '+str(script)]
else:
    command+=['-run=pythonscript','-script='+str(script)]
    command+=['-AllowCommandletRendering','-RenderOffscreen'] if args.task=='warm' else ['-NullRHI']
command+=['-unattended','-nosplash','-nosound','-NoLoadingScreen','-ForceTextureEncodeSpeed=Final','-abslog='+str(out/'Editor.log')]
unused=max(0,(os.cpu_count() or 8)-4)
settings=[('DevOptions.Shaders','ShaderCompilerCoreCountThreshold',999),('DevOptions.Shaders','NumUnusedShaderCompilingThreads',unused),
          ('DevOptions.Shaders','NumUnusedShaderCompilingThreadsDuringGame',unused),('SystemSettings','Editor.AsyncTextureCompilationMaxConcurrency',1),
          ('SystemSettings','Editor.AsyncStaticMeshCompilationMaxConcurrency',1),('SystemSettings','Editor.AsyncAssetCompilationMaxConcurrency',2),
          ('SystemSettings','Editor.AsyncAssetCompilationMaxMemoryUsage',2)]
command+=['-ini:Engine:'+','.join('['+section+']:'+name+'='+str(value) for section,name,value in settings)]
env=os.environ.copy();env.update(SPACECENTER_RUN_DIR=str(out),SPACECENTER_RESTORE_TASK=args.task,
                               SPACECENTER_RESTORE_MANIFEST=str(Path(args.manifest).resolve()),UE_SKIP_UBT_SDK_SETUP='1',
                               SPACECENTER_IMPORT_LIMIT=str(args.limit))
if args.task=='traverse':env.update(SPACECENTER_SUITE='traverse',SPACECENTER_PLAYERS='1',SPACECENTER_RESTORATION_VALIDATION='1')
metadata={'task':args.task,'command':command,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'script_sha256':sha(script),'manifest_sha256':sha(args.manifest)}
inspection_maps=[ROOT/'Content/Maps/SpaceCenter'/name for name in
                 ['L_SpaceCenterExtraction.umap','L_SpaceCenterNorth.umap']] if args.task in ['audit-integrated','capture-integrated'] else []
if inspection_maps:
    metadata['map_sha256_before']={p.relative_to(ROOT).as_posix():sha(p) for p in inspection_maps}
with (out/'Process.log').open('wb') as log:
    process=subprocess.Popen(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    metadata['pid']=process.pid;write_json(out/'Run.json',metadata)
    print('RESTORATION_EDITOR',process.pid,out,flush=True)
    try:metadata['exit_code']=process.wait(timeout=5400)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True)
        metadata['exit_code']=process.wait(timeout=30);metadata['error']='Owned editor timed out'
report=read(out/'Report.json') if (out/'Report.json').is_file() else {}
text=(out/'Editor.log').read_text(encoding='utf-8',errors='replace')
passed=metadata['exit_code']==0 and report.get('result')=='passed' and not any(s in text for s in
    ['Fatal error:','Traceback (most recent call last)','Failed to compile Material','LogMaterial: Error:'])
if inspection_maps:
    metadata['map_sha256_after']={p.relative_to(ROOT).as_posix():sha(p) for p in inspection_maps}
    if metadata['map_sha256_after']!=metadata['map_sha256_before']:
        passed=False;metadata['error']='Read-only inspection changed a production map'
metadata.update(result='passed' if passed else 'failed',finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
write_json(out/'Run.json',metadata)
print('RESTORATION_EDITOR_RESULT',metadata['result'],flush=True)
if not passed:
    for line in text.splitlines():
        if 'Error:' in line or 'Traceback' in line or 'Exception' in line:print(line.encode('ascii',errors='backslashreplace').decode(),flush=True)
sys.exit(0 if passed else 1)
