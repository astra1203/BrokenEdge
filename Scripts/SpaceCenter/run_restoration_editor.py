"""Run one owned editor stage with immutable logs and explicit source inputs."""
import argparse
import datetime
import os
import subprocess
import sys
from restoration_common import ROOT, WORK, PROFILE, PROFILE_FILE, input_path, read, sha, write_json

parser=argparse.ArgumentParser()
parser.add_argument('task',choices=['meshes','textures','materials','terrain-fidelity','collision-fidelity','warm','preview','capture','capture-integrated','probe-east','probe-east-integrated','traverse','traverse-integrated','audit','audit-integrated','integrate','cleanup-imports'])
parser.add_argument('--tag',required=True)
parser.add_argument('--manifest',help='Explicit source manifest; defaults to the active reconstruction marker')
parser.add_argument('--limit',type=int,default=0)
parser.add_argument('--players',type=int,choices=[1,2],default=1)
parser.add_argument('--probe-config')
parser.add_argument('--route',help='Explicit diagnostic traversal route; integration still requires the profile route')
parser.add_argument('--headless',action='store_true',help='Input-driven PIE without rendering; captures remain a separate stage')
parser.add_argument('--no-raytracing',action='store_true',help='Capture with hardware ray tracing disabled to reduce memory use')
parser.add_argument('--capture-texture-bias',type=int,choices=[0,1,2],default=0,help='Optional capture-only texture mip bias; recorded in Run.json')
parser.add_argument('--capture-world',choices=['pie','editor'],default='pie')
args=parser.parse_args()
if args.task.startswith('traverse'):
    route_file=(ROOT/args.route).resolve() if args.route else input_path('traverse_route',ROOT/'Scripts/SpaceCenter/route.json')
    if not route_file.is_file():parser.error('Regional traversal route is not ready; inspect the profile acceptance status or pass an explicit diagnostic --route')
if not args.manifest:
    marker=ROOT/'Scripts/SpaceCenter/restoration_active.json'
    if not marker.is_file():parser.error('Pass --manifest for the initial reconstruction')
    args.manifest=str(ROOT/read(marker)['manifest_file'])
manifest_profile=read(args.manifest).get('profile')
if manifest_profile:assert manifest_profile['sha256']==sha(PROFILE_FILE),'Manifest must be rebuilt after profile changes'
if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):parser.error('Use a new tag')
out=WORK/'EditorRuns'/args.tag
out.mkdir(parents=True,exist_ok=False)
subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'Scripts/Assert-EditorClosed.ps1'),
                '-ProjectFile',str(ROOT/'BrokenEdge.uproject')],check=True)
config=read(ROOT/'Scripts/SpaceCenter/north_config.json')
from pathlib import Path
engine=Path(config['engine']);rendered=args.task in ['capture','capture-integrated','probe-east','probe-east-integrated','traverse','traverse-integrated']
script=ROOT/'Scripts/SpaceCenter'/({'capture':'capture_restoration.py','capture-integrated':'capture_restoration.py','probe-east':'probe_east_route.py','probe-east-integrated':'probe_east_route.py','traverse':'validate_sample.py','traverse-integrated':'validate_sample.py'}.get(args.task,'import_restoration.py'))
command=[str(engine/'Engine/Binaries/Win64'/('UnrealEditor.exe' if rendered else 'UnrealEditor-Cmd.exe')),str(ROOT/'BrokenEdge.uproject')]
if rendered:
    map_name='/Game/Maps/SpaceCenter/L_SpaceCenterExtraction' if args.task.endswith('-integrated') else PROFILE.get('preview_map','/Game/Maps/SpaceCenter/L_SpaceCenterRestorationPreview')
    # Apply the accepted quality before the first rendered editor/PIE frame;
    # loading in default Epic first retains avoidable render allocations.
    startup=['r.VSync 0','t.MaxFPS 60','t.IdleWhenNotForeground 0','r.ScreenPercentage 100']
    startup+=['sg.'+group+'Quality 1' for group in ['ViewDistance','AntiAliasing','Shadow','GlobalIllumination','Reflection','PostProcess','Texture','Effects','Foliage','Shading']]
    command+=[map_name,'-RenderOffscreen','-windowed','-ResX=1920','-ResY=1080','-ForceRes','-ExecCmds='+','.join(startup+['py '+str(script)])]
    initial_quality=','.join('[ScalabilityGroups]:sg.'+group+'Quality=1' for group in ['ViewDistance','AntiAliasing','Shadow','GlobalIllumination','Reflection','PostProcess','Texture','Effects','Foliage','Shading'])
    command+=['-ini:'+kind+':'+initial_quality for kind in ['EditorSettings','GameUserSettings']]
else:
    command+=['-run=pythonscript','-script='+str(script)]
    command+=['-AllowCommandletRendering','-RenderOffscreen'] if args.task=='warm' else ['-NullRHI']
command+=['-unattended','-nosplash','-nosound','-NoLoadingScreen','-SCCProvider=None','-ForceTextureEncodeSpeed=Final','-abslog='+str(out/'Editor.log')]
if args.no_raytracing:
    assert args.task.startswith('capture'),'Ray tracing override is capture-only'
    command+=['-noraytracing']
if args.task.startswith('probe-east'):command+=['-NullRHI']
if args.headless:
    assert args.task.startswith('traverse'),'Headless applies only to traversal'
    command+=['-NullRHI']
unused=max(0,(os.cpu_count() or 8)-4)
settings=[('DevOptions.Shaders','ShaderCompilerCoreCountThreshold',999),('DevOptions.Shaders','NumUnusedShaderCompilingThreads',unused),
          ('DevOptions.Shaders','NumUnusedShaderCompilingThreadsDuringGame',unused),('SystemSettings','Editor.AsyncTextureCompilationMaxConcurrency',1),
          ('SystemSettings','Editor.AsyncStaticMeshCompilationMaxConcurrency',1),('SystemSettings','Editor.AsyncAssetCompilationMaxConcurrency',2),
          ('SystemSettings','Editor.AsyncAssetCompilationMaxMemoryUsage',2)]
if args.capture_texture_bias:
    assert args.task.startswith('capture'),'Texture bias override is capture-only'
    settings += [('SystemSettings','r.Streaming.MipBias',args.capture_texture_bias),
                 ('SystemSettings','r.Streaming.PoolSize',512),
                 ('SystemSettings','r.Streaming.MaxTempMemoryAllowed',64)]
command+=['-ini:Engine:'+','.join('['+section+']:'+name+'='+str(value) for section,name,value in settings)]
env=os.environ.copy();env.update(SPACECENTER_RUN_DIR=str(out),SPACECENTER_RESTORE_TASK=args.task,
                               SPACECENTER_RESTORE_MANIFEST=str(Path(args.manifest).resolve()),UE_SKIP_UBT_SDK_SETUP='1',
                               SPACECENTER_IMPORT_LIMIT=str(args.limit))
env['SPACECENTER_CAPTURE_WORLD']=args.capture_world
if args.headless:env['SPACECENTER_HEADLESS']='1'
if args.probe_config:env['SPACECENTER_PROBE_CONFIG']=str((ROOT/args.probe_config).resolve())
if args.task.startswith('traverse'):env.update(SPACECENTER_SUITE='traverse',SPACECENTER_PLAYERS=str(args.players),SPACECENTER_RESTORATION_VALIDATION='1',
    SPACECENTER_TRAVERSE_ROUTE=str((ROOT/args.route).resolve() if args.route else input_path('traverse_route',ROOT/'Scripts/SpaceCenter/route.json')))
if args.task.startswith('traverse') and PROFILE['phase']=='7E':
    env['SPACECENTER_VALIDATION_TIMEOUT']=str(1800*args.players)
metadata={'task':args.task,'command':command,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'script_sha256':sha(script),'manifest_sha256':sha(args.manifest),
          'profile_file':PROFILE_FILE.relative_to(ROOT).as_posix(),'profile_sha256':sha(PROFILE_FILE),
          'capture_world':args.capture_world if args.task.startswith('capture') else None}
if args.task.startswith('capture'):
    metadata.update(raytracing_disabled=args.no_raytracing,capture_texture_bias=args.capture_texture_bias)
if (WORK/'NativeAssets.json').is_file():metadata['native_assets_sha256']=sha(WORK/'NativeAssets.json')
if args.task.startswith('traverse'):
    metadata.update(route_file=str(route_file),route_sha256=sha(route_file),headless=args.headless)
inspection_maps=[ROOT/'Content/Maps/SpaceCenter'/name for name in
                 ['L_SpaceCenterExtraction.umap','L_SpaceCenterNorth.umap']] if args.task.endswith('-integrated') else []
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
    ['Fatal error:','Traceback (most recent call last)','Failed to compile Material','LogMaterial: Error:','Ensure condition failed:'])
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
