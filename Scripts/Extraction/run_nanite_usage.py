"""Launch one recorded, bounded Nanite-usage repair or fresh-process verification.

Examples (new tags are mandatory):
  python Scripts/Extraction/run_nanite_usage.py --tag usage-repair01
  python Scripts/Extraction/run_nanite_usage.py --tag usage-verify01 --verify-only \
    --baseline-report Saved/Extraction/NaniteUsage/usage-repair01/Report.json

This wrapper does not build, cook, invoke Git, or load a map. It runs the fixed
repair script in a new UnrealEditor-Cmd process. Success requires both exit 0
and a passed Report.json. Use resource_guard.py externally when appropriate.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys

from repair_nanite_usage import sha, validate_manifest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Common.ue_env import engine_root, run_record


def create_run_directory(root, tag):
    if not tag or not all(character.isascii() and (character.isalnum() or character in '-_') for character in tag):
        raise ValueError('Use a new ASCII alphanumeric, hyphen or underscore tag')
    out = root / 'Saved/Extraction/NaniteUsage' / tag
    out.mkdir(parents=True, exist_ok=False)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--baseline-report', type=Path)
    parser.add_argument('--engine', type=Path)
    parser.add_argument('--timeout-seconds', type=int, default=1800)
    parser.add_argument('--shader-workers', type=int, default=2)
    args = parser.parse_args()
    if args.timeout_seconds <= 0 or args.shader_workers <= 0:
        parser.error('timeout-seconds and shader-workers must be positive')
    if args.verify_only != bool(args.baseline_report):
        parser.error('--verify-only requires --baseline-report; repair must not use a baseline')
    try:
        engine = engine_root(args.engine)
    except (ValueError, OSError, KeyError) as error:
        parser.error(str(error))
    executable = engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    baseline = (ROOT / args.baseline_report).resolve() if args.baseline_report else None
    if baseline and not baseline.is_file():
        parser.error('Baseline Report.json does not exist')
    manifest = ROOT / 'Scripts/Extraction/nanite_usage_manifest.json'
    validate_manifest(json.loads(manifest.read_text(encoding='utf-8-sig')))
    script = ROOT / 'Scripts/Extraction/repair_nanite_usage.py'
    out = create_run_directory(ROOT, args.tag)
    mode = 'verify' if args.verify_only else 'repair'
    command = [str(executable), str(ROOT / 'BrokenEdge.uproject'), '-run=pythonscript', '-script=' + str(script),
               '-NullRHI', '-unattended', '-nosplash', '-nosound', '-NoLoadingScreen', '-SCCProvider=None',
               '-abslog=' + str(out / 'Editor.log')]
    unused = max(0, (os.cpu_count() or 8) - args.shader_workers)
    command.append('-ini:Engine:[DevOptions.Shaders]:ShaderCompilerCoreCountThreshold=999,'
                   'NumUnusedShaderCompilingThreads=' + str(unused) + ','
                   'NumUnusedShaderCompilingThreadsDuringGame=' + str(unused))
    environment = os.environ.copy()
    environment.update(EXTRACTION_NANITE_RUN_DIR=str(out), EXTRACTION_NANITE_MODE=mode,
                       EXTRACTION_NANITE_ENGINE_ROOT=str(engine), UE_SKIP_UBT_SDK_SETUP='1')
    if baseline:
        environment['EXTRACTION_NANITE_BASELINE'] = str(baseline)
    else:
        environment.pop('EXTRACTION_NANITE_BASELINE', None)
    metadata = dict(mode=mode, command=command, started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    task_script_sha256=sha(script), wrapper_sha256=sha(Path(__file__)), manifest_sha256=sha(manifest),
                    baseline_report=str(baseline) if baseline else None, shader_workers=args.shader_workers)
    run_file = out / 'Run.json'
    metadata.update(run_record(command, run_file, engine=engine, script=__file__))
    try:
        subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                        str(ROOT / 'Scripts/Assert-EditorClosed.ps1'), '-ProjectFile', str(ROOT / 'BrokenEdge.uproject')],
                       check=True, cwd=ROOT)
        with (out / 'Process.log').open('wb') as log:
            process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            metadata['pid'] = process.pid
            run_file.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
            print('NANITE_USAGE_RUN', out, 'PID', process.pid, flush=True)
            try:
                metadata['exit_code'] = process.wait(timeout=args.timeout_seconds)
            except subprocess.TimeoutExpired:
                # Terminate only this wrapper's known commandlet process tree.
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
                metadata['error'] = 'Owned editor task timed out'
                metadata['exit_code'] = process.wait(timeout=30)
    except Exception as error:
        metadata['error'] = f'{type(error).__name__}: {error}'
    metadata['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    report_file = out / 'Report.json'
    try:
        report = json.loads(report_file.read_text(encoding='utf-8-sig')) if report_file.is_file() else {}
    except (OSError, ValueError) as error:
        report = {}
        metadata['error'] = 'Cannot read asset task report: ' + str(error)
    metadata['report_passed'] = report.get('result') == 'passed' and report.get('mode') == mode
    metadata['passed'] = metadata.get('exit_code') == 0 and metadata['report_passed'] and 'error' not in metadata
    run_file.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print('NANITE_USAGE_EXIT', metadata.get('exit_code'), 'PASSED', metadata['passed'], flush=True)
    return 0 if metadata['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
