"""Recorded UE 5.8.3 builds and Development packages; never overwrite a run."""
import argparse
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding='utf-8')
ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path('C:/UnrealProjects/UnrealEngine-5.8.3-release')
parser = argparse.ArgumentParser()
parser.add_argument('--target', choices=['Editor', 'Client', 'Server'], default='Editor')
parser.add_argument('--package', action='store_true')
parser.add_argument('--no-uba', action='store_true', help='Use UE non-detoured local builds for this process tree through UBT environment configuration')
parser.add_argument('--jobs', type=int, default=0, help='Optional UBT compile action limit; 0 uses the engine default')
parser.add_argument('--cook-workers', type=int, default=0, help='Optional shader worker limit and bounded async asset compilation during cooking')
parser.add_argument('--tag', required=True)
args = parser.parse_args()
if args.jobs < 0:
    raise ValueError('jobs must be zero or positive')
if args.cook_workers < 0:
    raise ValueError('cook-workers must be zero or positive')
if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
    raise ValueError('Use an alphanumeric run tag')
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
if args.package:
    if args.target == 'Editor':
        raise ValueError('Choose Client or Server for packaging')
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT / 'Scripts/Assert-EditorClosed.ps1'),
                    '-ProjectFile', str(ROOT / 'BrokenEdge.uproject')], check=True)
    command = [str(ENGINE / 'Engine/Build/BatchFiles/RunUAT.bat'), 'BuildCookRun', '-project=' + str(ROOT / 'BrokenEdge.uproject'),
               '-nop4', '-build', '-cook', '-stage', '-pak', '-iostore', '-archive',
               '-archivedirectory=' + str(ROOT / 'Packaged' / args.target),
               '-map=/Game/Maps/Lobby+/Game/Maps/FreeForAll+/Game/Maps/Teams+/Game/Maps/SpaceCenter/L_SpaceCenterExtraction',
               '-prereqs', '-ubtargs=-NoHotReloadFromIDE -WaitMutex' + (f' -MaxParallelActions={args.jobs}' if args.jobs else ''), '-utf8output']
    command += ['-server', '-noclient', '-serverplatform=Win64', '-serverconfig=Development'] if args.target == 'Server' else ['-platform=Win64', '-clientconfig=Development']
    if args.cook_workers:
        unused=max(0,(os.cpu_count() or 8)-args.cook_workers)
        settings=[('DevOptions.Shaders','ShaderCompilerCoreCountThreshold',999),
                  ('DevOptions.Shaders','NumUnusedShaderCompilingThreads',unused),
                  ('DevOptions.Shaders','NumUnusedShaderCompilingThreadsDuringGame',unused),
                  ('SystemSettings','Editor.AsyncTextureCompilationMaxConcurrency',1),
                  ('SystemSettings','Editor.AsyncStaticMeshCompilationMaxConcurrency',1),
                  ('SystemSettings','Editor.AsyncAssetCompilationMaxConcurrency',2),
                  ('SystemSettings','Editor.AsyncAssetCompilationMaxMemoryUsage',2)]
        command += ['-AdditionalCookerOptions=-ForceTextureEncodeSpeed=Final -ini:Engine:'+','.join(
            '['+section+']:'+name+'='+str(value) for section,name,value in settings)
            +' -ini:Editor:[CookSettings]:MemoryMinFreePhysical=4096,[CookSettings]:MemoryMaxUsedPhysical=8192']
else:
    target = 'BrokenEdge' + (args.target if args.target != 'Client' else '')
    command = [str(ENGINE / 'Engine/Build/BatchFiles/Build.bat'), target, 'Win64', 'Development',
               '-Project=' + str(ROOT / 'BrokenEdge.uproject'), '-WaitMutex', '-NoHotReloadFromIDE']
    if args.jobs:
        command.append(f'-MaxParallelActions={args.jobs}')
metadata = {'command': command, 'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
start = time.monotonic()
environment = os.environ.copy()
overrides = {}
if args.package:
    overrides['UE_SKIP_UBT_SDK_SETUP'] = '1'
if args.no_uba:
    # UAT nests -ubtargs inside each target; global executor switches there
    # are not reliable there. UE 5.8 XmlConfig reads process settings last.
    # ExecutorFactory still uses UBA but bAllowUBAExecutor=false disables detours.
    overrides['UnrealBuildTool_BuildConfiguration__bAllowUBAExecutor'] = 'false'
if overrides:
    environment.update(overrides)
    metadata['environment_overrides'] = overrides
with (out / 'Build.log').open('wb') as log:
    process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    metadata['pid'] = process.pid
    (out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print('BUILD_STARTED', process.pid, out, flush=True)
    metadata['exit_code'] = process.wait()
metadata['elapsed_seconds'] = time.monotonic() - start
if args.package:
    # UAT writes the detailed child build output separately from its console log.
    for child, name in [('UBA-BrokenEdgeEditor-Win64-Development.txt', 'Ubt.log'),
                        ('UBA-BootstrapPackagedGame-Win64-Shipping.txt', 'Bootstrap.log')]:
        child_log = ENGINE / 'Engine/Programs/AutomationTool/Saved/Logs' / child
        if child_log.exists() and child_log.stat().st_mtime >= datetime.datetime.fromisoformat(metadata['started_utc']).timestamp():
            shutil.copy2(child_log, out / name)
if args.package and metadata['exit_code'] == 0:
    platform = 'WindowsServer' if args.target == 'Server' else 'Windows'
    for launcher in (['StartServer.bat'] if args.target == 'Server' else ['StartClient.bat']):
        source = ROOT / 'Deployment' / launcher
        if source.exists():
            shutil.copy2(source, ROOT / 'Packaged' / args.target / platform / launcher)
(out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
for line in (out / 'Build.log').read_bytes().splitlines():
    if b'error ' in line.lower() or b'Result:' in line or b'AutomationTool exiting' in line:
        try:
            print(line.decode('utf-8'))
        except UnicodeDecodeError:
            print(line.decode('gb18030', errors='replace'))
print('BUILD_EXIT', metadata['exit_code'], flush=True)
sys.exit(metadata['exit_code'])
