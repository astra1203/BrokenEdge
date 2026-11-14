"""Run the runtime-asset acceptance test from a recorded Development package."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from pe_validation import validate_executable

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Common.ue_env import run_record

TEST = 'BrokenEdge.Extraction.RuntimeAssets.TrainingInventory'
COMBAT_TESTS = {'BrokenEdge.Extraction.CombatMode.' + name for name in (
    'DispatchAndOwnership', 'LegacyLifecycle', 'LegacyRequestValidation', 'PredictionAndCadenceIsolation')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-report', type=Path, required=True, help='Successful run_build.py Client package Run.json')
    parser.add_argument('--tag', required=True)
    parser.add_argument('--suite', choices=['assets', 'combat'], default='assets')
    args = parser.parse_args()
    if not args.tag or not all(c.isascii() and (c.isalnum() or c in '-_') for c in args.tag):
        parser.error('Use a new ASCII alphanumeric, hyphen or underscore tag')
    build_path = args.build_report.resolve()
    try:
        build = json.loads(build_path.read_text(encoding='utf-8'))
        if build.get('exit_code') != 0 or not build.get('archive_root') or build.get('compile_only'):
            raise ValueError('A successful package build report is required')
        if '/Game/Extraction/Training/L_WeaponRange' not in build.get('package_maps', []):
            raise ValueError('Build must explicitly include the training map; use run_build.py --include-training-map')
        executable = Path(build['executable']['path']).resolve()
        if not executable.is_relative_to(ROOT / 'Packaged') or executable.name != 'BrokenEdge.exe':
            raise ValueError('Expected a Client executable inside this project Packaged directory')
        binary = validate_executable(executable)
        if binary['sha256'] != build['executable']['sha256']:
            raise ValueError('Packaged executable changed since the recorded build')
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    out = ROOT / 'Saved/Extraction' / args.tag
    out.mkdir(parents=True, exist_ok=False)
    expected = {TEST} if args.suite == 'assets' else COMBAT_TESTS
    test_filter = TEST if args.suite == 'assets' else 'BrokenEdge.Extraction.CombatMode.'
    command = [str(executable), '/Game/Extraction/Training/L_WeaponRange',
               '-unattended', '-nosplash', '-nosound', '-NoLoadingScreen',
               '-RenderOffscreen', '-ResX=1280', '-ResY=720',
               '-RuntimeAssetsScreenshot=' + str(out / 'Inventory.png'),
               '-ExecCmds=Automation RunTests ' + test_filter,
               '-TestExit=Automation Test Queue Empty',
               '-ReportExportPath=' + str(out / 'Automation'), '-abslog=' + str(out / 'Game.log')]
    metadata = run_record(command, out / 'Run.json', engine=build['engine_root'], script=__file__)
    metadata.update(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    build_report=str(build_path), build_report_sha256=hashlib.sha256(build_path.read_bytes()).hexdigest(),
                    build_head=build['git_head'], build_engine_version=build['engine_version'], executable=binary,
                    suite=args.suite, expected_tests=sorted(expected))
    with (out / 'Process.log').open('wb') as log:
        process = subprocess.Popen(command, cwd=executable.parent, stdout=log, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        metadata['pid'] = process.pid
        (out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
        print('PACKAGED_ASSETS_STARTED', process.pid, out, flush=True)
        try:
            metadata['exit_code'] = process.wait(timeout=600)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
            metadata['exit_code'] = process.wait(timeout=30)
            metadata['error'] = 'Packaged asset test timed out'
    try:
        text = (out / 'Game.log').read_text(encoding='utf-8', errors='replace')
        automation = json.loads((out / 'Automation/index.json').read_text(encoding='utf-8-sig'))
        tests = automation.get('tests', [])
        metadata['successful_tests'] = sum(test.get('state') == 'Success' for test in tests)
        passed = (metadata['exit_code'] == 0 and 'error' not in metadata
                  and len(tests) == len(expected) and {test.get('fullTestPath') for test in tests} == expected
                  and metadata['successful_tests'] == len(expected) and automation.get('failed') == 0
                  and 'Fatal error:' not in text and 'Traceback (most recent call last)' not in text)
        if args.suite == 'assets':
            passed = passed and 'RUNTIME_ASSETS_COMPLETE' in text and (out / 'Inventory.png').is_file()
    except (OSError, ValueError) as error:
        metadata['error'] = str(error)
        passed = False
    metadata['result'] = 'passed' if passed else 'failed'
    metadata['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print('PACKAGED_ASSETS_RESULT', metadata['result'], out, flush=True)
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
