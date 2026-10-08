"""Recorded UE 5.8.3 builds and Development packages; never overwrite a run."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from pe_validation import validate_executable

sys.stdout.reconfigure(encoding='utf-8')
ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path('C:/UnrealProjects/UnrealEngine-5.8.3-release')
parser = argparse.ArgumentParser()
parser.add_argument('--target', choices=['Editor', 'Client', 'Server'], default='Editor')
parser.add_argument('--package', action='store_true')
parser.add_argument('--package-root', help='Separate archive root containing Client/Server; must be inside Packaged')
parser.add_argument('--file-cook', action='store_true', help='Fresh isolated filesystem cook with SkipZenStore; still builds IoStore packages')
parser.add_argument('--cook-root', help='New short absolute cook directory, required with --file-cook to avoid Windows package path limits')
parser.add_argument('--direct-stage', action='store_true', help='Stage the fresh distributable directly in the archive root, avoiding a duplicate archive copy')
parser.add_argument('--no-uba', action='store_true', help='Use UE non-detoured local builds for this process tree through UBT environment configuration')
parser.add_argument('--compile-only', action='store_true', help='Compile objects with UBT -NoLink; never report an executable from this step')
parser.add_argument('--jobs', type=int, default=0, help='Optional UBT compile action limit; 0 uses the engine default')
parser.add_argument('--cook-workers', type=int, default=0, help='Optional shader worker limit and bounded async asset compilation during cooking')
parser.add_argument('--tag', required=True)
args = parser.parse_args()
if args.jobs < 0:
    raise ValueError('jobs must be zero or positive')
if args.cook_workers < 0:
    raise ValueError('cook-workers must be zero or positive')
if args.compile_only and args.package:
    parser.error('--compile-only cannot package')
if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
    raise ValueError('Use an alphanumeric run tag')
if (args.package_root or args.file_cook or args.cook_root) and not args.package:
    parser.error('--package-root and --file-cook require --package')
if args.direct_stage and not (args.package and args.package_root and args.file_cook):
    parser.error('--direct-stage requires a fresh --package-root and --file-cook')
if args.file_cook != bool(args.cook_root):
    parser.error('--file-cook and --cook-root must be used together')
cook_root = Path(args.cook_root).resolve() if args.cook_root else None
if cook_root and (not Path(args.cook_root).is_absolute() or cook_root.exists()):
    parser.error('--cook-root must be a new absolute directory; existing cooks are never reused')
archive_root = (ROOT / (args.package_root or 'Packaged')).resolve()
if not archive_root.is_relative_to((ROOT / 'Packaged').resolve()):
    parser.error('--package-root must be inside this project Packaged directory')
archive = archive_root / args.target
if args.package_root and archive.exists():
    parser.error('Use a new archive target; existing packages are never overwritten with --package-root')
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
if args.package:
    if args.target == 'Editor':
        raise ValueError('Choose Client or Server for packaging')
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT / 'Scripts/Assert-EditorClosed.ps1'),
                    '-ProjectFile', str(ROOT / 'BrokenEdge.uproject')], check=True)
    command = [str(ENGINE / 'Engine/Build/BatchFiles/RunUAT.bat'), 'BuildCookRun', '-project=' + str(ROOT / 'BrokenEdge.uproject'),
               '-nop4', '-build', '-cook', '-stage', '-pak', '-iostore', '-archive',
               '-archivedirectory=' + str(archive),
               '-map=/Game/Maps/Lobby+/Game/Maps/FreeForAll+/Game/Maps/Teams+/Game/Maps/SpaceCenter/L_SpaceCenterExtraction',
               '-prereqs', '-ubtargs=-NoHotReloadFromIDE -WaitMutex' + (f' -MaxParallelActions={args.jobs}' if args.jobs else ''), '-utf8output']
    command += ['-server', '-noclient', '-serverplatform=Win64', '-serverconfig=Development'] if args.target == 'Server' else ['-platform=Win64', '-clientconfig=Development']
    if args.direct_stage:
        command.remove('-archive')
        command.remove('-archivedirectory=' + str(archive))
    cooker_options = []
    if args.file_cook:
        # A fresh output avoids carrying a stale Zen projectstore/file manifest
        # into staging. DDC may still use Zen; cooked package storage does not.
        cook_platform = 'WindowsServer' if args.target == 'Server' else 'Windows'
        # CookCommandlet writes exactly OutputDir; UAT staging appends the
        # platform unless the supplied directory already ends in its name.
        command += ['-CookOutputDir=' + str(cook_root / cook_platform), '-stagingdirectory=' + str(archive if args.direct_stage else out / 'Staged')]
        cooker_options.append('-SkipZenStore')
    if args.cook_workers:
        unused=max(0,(os.cpu_count() or 8)-args.cook_workers)
        settings=[('DevOptions.Shaders','ShaderCompilerCoreCountThreshold',999),
                  ('DevOptions.Shaders','NumUnusedShaderCompilingThreads',unused),
                  ('DevOptions.Shaders','NumUnusedShaderCompilingThreadsDuringGame',unused),
                  ('SystemSettings','Editor.AsyncTextureCompilationMaxConcurrency',1),
                  ('SystemSettings','Editor.AsyncStaticMeshCompilationMaxConcurrency',1),
                  ('SystemSettings','Editor.AsyncAssetCompilationMaxConcurrency',2),
                  ('SystemSettings','Editor.AsyncAssetCompilationMaxMemoryUsage',2)]
        cooker_options.append('-ForceTextureEncodeSpeed=Final -ini:Engine:'+','.join(
            '['+section+']:'+name+'='+str(value) for section,name,value in settings)
            +' -ini:Editor:[CookSettings]:MemoryMinFreePhysical=4096,[CookSettings]:MemoryMaxUsedPhysical=8192')
    if cooker_options:
        command.append('-AdditionalCookerOptions=' + ' '.join(cooker_options))
else:
    target = 'BrokenEdge' + (args.target if args.target != 'Client' else '')
    command = [str(ENGINE / 'Engine/Build/BatchFiles/Build.bat'), target, 'Win64', 'Development',
               '-Project=' + str(ROOT / 'BrokenEdge.uproject'), '-WaitMutex', '-NoHotReloadFromIDE']
    if args.jobs:
        command.append(f'-MaxParallelActions={args.jobs}')
    if args.compile_only:
        command.append('-NoLink')
metadata = {'command': command, 'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'compile_only': args.compile_only}
if args.package:
    metadata.update(archive_root=str(archive_root), direct_stage=args.direct_stage, cook_store='filesystem' if args.file_cook else 'project-default')
    if cook_root:
        metadata['cook_root'] = str(cook_root)
metadata['input_sha256'] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in [
    'Scripts/Extraction/run_build.py', 'Source/BrokenEdge/Tests/ExtractionDriverTests.cpp',
    'Config/DefaultGame.ini', 'Config/DefaultEngine.ini', 'BrokenEdge.uproject',
    'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap', 'Content/Maps/SpaceCenter/L_SpaceCenterNorth.umap']}
for source in sorted((ROOT/'Source/BrokenEdge').rglob('*')):
    if source.suffix in ('.h','.cpp'):
        metadata['input_sha256'][source.relative_to(ROOT).as_posix()] = hashlib.sha256(source.read_bytes()).hexdigest()
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
if metadata['exit_code']==0 and not args.compile_only and args.target in ('Client','Server'):
    executable='BrokenEdgeServer.exe' if args.target=='Server' else 'BrokenEdge.exe'
    binary=(archive / ('WindowsServer' if args.target=='Server' else 'Windows') / 'BrokenEdge/Binaries/Win64' / executable
            if args.package else ROOT/'Binaries/Win64'/executable)
    try:
        metadata['executable']=validate_executable(binary)
    except (OSError,ValueError) as error:
        metadata.update(exit_code=1,error=str(error))
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
            shutil.copy2(source, archive / platform / launcher)
(out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
for line in (out / 'Build.log').read_bytes().splitlines():
    if b'error ' in line.lower() or b'Result:' in line or b'AutomationTool exiting' in line:
        try:
            print(line.decode('utf-8'))
        except UnicodeDecodeError:
            print(line.decode('gb18030', errors='replace'))
print('BUILD_EXIT', metadata['exit_code'], flush=True)
sys.exit(metadata['exit_code'])
