"""Native 1080p screenshots in one standalone Development game world, without PIE duplication."""
import argparse
import ctypes
import datetime
import json
import math
import msvcrt
import os
from pathlib import Path
import struct
import subprocess
import time
import sys
sys.path.insert(0,str(Path.cwd()/'Scripts/SpaceCenter'))
from restoration_common import *

p=argparse.ArgumentParser();p.add_argument('--tag',required=True);p.add_argument('--manifest',required=True)
p.add_argument('--task',choices=['capture','traverse'],default='capture')
p.add_argument('--probe-report',help='Passed native PIE floor/terrain probe directory required for traversal')
p.add_argument('--nullrhi',action='store_true',help='Validate traversal/collision without rendering; visual captures remain a separate gate')
p.add_argument('--integrated',action='store_true');args=p.parse_args()
if args.task=='traverse' and not args.probe_report:p.error('Traversal requires --probe-report')
if args.nullrhi and args.task!='traverse':p.error('--nullrhi is only valid for traversal')
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
manifest=read(args.manifest)
if manifest.get('profile'):assert manifest['profile']['sha256']==sha(PROFILE_FILE)
out=WORK/'EditorRuns'/args.tag;out.mkdir(parents=True,exist_ok=False)
cameras_file=input_path('cameras',ROOT/'Scripts/SpaceCenter/restoration_cameras_final.json');config=read(cameras_file)
if PROFILE['phase']=='7D':assert len(config['shots'])>=20
subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'Scripts/Assert-EditorClosed.ps1'),
                '-ProjectFile',str(ROOT/'BrokenEdge.uproject')],check=True)
engine=Path(read(ROOT/'Scripts/SpaceCenter/north_config.json')['engine'])
map_name='/Game/Maps/SpaceCenter/L_SpaceCenterExtraction' if args.integrated else PROFILE.get('preview_map','/Game/Maps/SpaceCenter/L_SpaceCenterRestorationPreview')
groups=['ViewDistance','AntiAliasing','Shadow','GlobalIllumination','Reflection','PostProcess','Texture','Effects','Foliage','Shading']
settings=['r.VSync 0','r.ScreenPercentage 100','t.MaxFPS 60','t.IdleWhenNotForeground 0']+['sg.'+s+'Quality 1' for s in groups]
url=map_name+'?game=/Game/Characters/Stinger/Blueprints/BP_StingerTestGameMode.BP_StingerTestGameMode_C'
command=[str(engine/'Engine/Binaries/Win64/UnrealEditor.exe'),str(ROOT/'BrokenEdge.uproject'),url,'-game',
    '-RenderOffscreen','-windowed','-ResX=1920','-ResY=1080','-ForceRes','-unattended','-nosplash','-nosound',
    '-NoLoadingScreen','-ExtractionTestRole=A','-ExtractionTestDir='+str(out),'-ExtractionStore='+str(out/'Store'),
    '-ExecCmds='+','.join(settings+['Automation RunTests BrokenEdge.Extraction.ProcessDriver']),
    '-TestExit=Automation Test Queue Empty','-abslog='+str(out/'Editor.log'),
    '-ini:GameUserSettings:'+','.join('[ScalabilityGroups]:sg.'+g+'Quality=1' for g in groups)]
if args.nullrhi:command.append('-NullRHI')
maps=[ROOT/'Content/Maps/SpaceCenter'/name for name in ['L_SpaceCenterExtraction.umap','L_SpaceCenterNorth.umap']] if args.integrated else [ROOT/'Content'/Path(map_name.removeprefix('/Game/')).with_suffix('.umap')]
run={'task':args.task+('-integrated' if args.integrated else ''),'result':'running','command':command,
     'manifest_sha256':sha(args.manifest),'profile_sha256':sha(PROFILE_FILE),'script_sha256':sha(__file__),
     'driver_sha256':sha(ROOT/'Source/BrokenEdge/Tests/ExtractionDriverTests.cpp'),
     'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
     'map_sha256_before':{p.relative_to(ROOT).as_posix():sha(p) for p in maps}}
report={'task':args.task,'result':'running','map':map_name,'captures':[],'resolution':config['resolution'],
        'quality':config['quality'],'camera_config_sha256':sha(cameras_file),'reference_status':config['reference_status'],
        'capture_world':'standalone','render_boundary':'One uncooked Development game world with the existing local test GameMode. Real native viewport screenshots, no editor or PIE world and no image scaling. Packaged gameplay is separately accepted.'}
if args.nullrhi:
    report.update(resolution=None,quality=None,render_boundary='One uncooked Development game world using NullRHI. Real character input, movement and collision; no rendered screenshots or visual/performance claims. Native 1080p Medium visuals are accepted separately.')
process=None;counter=0;last={};started=time.monotonic()
kernel=ctypes.WinDLL('kernel32',use_last_error=True)
kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
kernel.CreateFileW.restype=ctypes.c_void_p


def state():
    global last
    handle=kernel.CreateFileW(str(out/'A-state.json'),0x80000000,7,None,3,0x80,None)
    if handle==ctypes.c_void_p(-1).value:return last
    try:
        with os.fdopen(msvcrt.open_osfhandle(handle,os.O_RDONLY|os.O_BINARY),'r',encoding='utf-8-sig') as f:last=json.load(f)
    except (OSError,ValueError):pass
    return last


def wait(predicate,label,timeout=60):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        if predicate():return
        if process.poll() is not None:raise RuntimeError('Game exited during '+label)
        time.sleep(.15)
    raise TimeoutError(label)


def send(op,**fields):
    global counter
    counter+=1;file=out/'A-command.json';temporary=file.with_suffix('.tmp')
    temporary.write_text(json.dumps({'id':counter,'op':op,**fields}),encoding='utf-8')
    deadline=time.monotonic()+10
    while True:
        try:temporary.replace(file);break
        except PermissionError:
            if time.monotonic()>deadline:raise
            time.sleep(.02)
    wait(lambda:state().get('command',0)>=counter,op)
    assert not state().get('error'),state().get('error')


def screenshot(shot):
    current=state()
    assert [current['viewport_width'],current['viewport_height']]==config['resolution']
    expected={**{'sg.'+g+'Quality':1 for g in groups},'r.ScreenPercentage':100,'r.VSync':0,'t.MaxFPS':60}
    assert current['quality']==expected,current['quality']
    send('screenshot',name=shot['name'],show_ui=False)
    path=out/('A-'+shot['name']+'.png');wait(path.is_file,'screenshot file');time.sleep(.5)
    data=path.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n'
    size=list(struct.unpack('>II',data[16:24]));assert size==config['resolution']
    report.update(viewport_size=size,native_render_resolution=size,runtime_quality=current['quality'])
    report['captures'].append(shot|{'file':path.name,'sha256':sha(path),'size':size})
    write_json(out/'Report.json',report);print('RUNTIME_CAPTURE_SHOT',shot['name'],flush=True)


def traverse():
    current=state()['players'][0]
    send('fixture',players=[{'name':current['name'],'position':[-2100,-300,460],'health':100,'shield':0}])
    write_json(out/'InitialState.json',state())
    print('PARKING_DEBUG_READY',process.pid,flush=True)
    wait(lambda:(out/'StartWalk.txt').exists(),'debugger attached',600)
    send('walk',points=[[-1500,-300,362]])
    wait(lambda:(out/'FinishDebug.txt').exists(),'debugger evidence collected',600)
    write_json(out/'FinalState.json',state())


try:
    with (out/'Process.log').open('wb') as log:
        process=subprocess.Popen(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        run['pid']=process.pid;write_json(out/'Run.json',run)
        print('RUNTIME_CAPTURE',process.pid,out,flush=True)
        wait(lambda:state().get('local_players')==1,'standalone world',180)
        if args.task=='traverse':traverse()
        else:
            for shot in config['shots']:
                send('world_camera',position=shot['position'],target=shot['target'],field_of_view=config['field_of_view'])
                wait(lambda:math.dist(state()['view_position'],shot['position'])<1,'camera placement')
                time.sleep(12);screenshot(shot)
        send('finish');run['exit_code']=process.wait(timeout=45)
    assert run['exit_code']==0
    logtext=(out/'Editor.log').read_text(encoding='utf-8',errors='replace')
    assert 'EXTRACTION_DRIVER_OK A' in logtext
    assert not any(s in logtext for s in ['Fatal error:','Ensure condition failed:','Failed to compile Material','LogMaterial: Error:'])
    report['result']=run['result']='passed'
except Exception as error:
    report.update(result='failed',error=repr(error));run.update(result='failed',error=repr(error));raise
finally:
    if process and process.poll() is None:
        subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True)
        process.wait(timeout=30)
    run['map_sha256_after']={p.relative_to(ROOT).as_posix():sha(p) for p in maps}
    if run['map_sha256_after']!=run['map_sha256_before']:run.update(result='failed',error='Read-only capture changed a map');report['result']='failed'
    run['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();report['elapsed_seconds']=time.monotonic()-started
    write_json(out/'Run.json',run);write_json(out/'Report.json',report)
if run['result']!='passed':raise SystemExit(1)
