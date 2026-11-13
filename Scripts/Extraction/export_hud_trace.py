"""Export recorded HUD CPU scopes with Unreal Insights, without opening its UI."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Common.ue_env import engine_root, run_record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path, help='Directory containing HUD.utrace')
    parser.add_argument('--engine', type=Path)
    args = parser.parse_args()
    engine = engine_root(args.engine)
    output = args.run.resolve()
    trace = output / 'HUD.utrace'
    assert trace.is_file(), trace
    destination = output / 'HUDTimingEvents.csv'
    assert not destination.exists(), 'Refusing to overwrite an existing export'
    if any(character.isspace() for character in destination.as_posix()):
        parser.error('Use a trace export directory without whitespace (Insights command parser).')
    export = (f'TimingInsights.ExportTimingEvents {destination.as_posix()} '
              '-threads=GameThread -timers=ExtractionHUDRefresh -columns=TimerName,StartTime,EndTime,Duration')
    command = [str(engine / 'Engine/Binaries/Win64/UnrealInsights.exe'), '-NoUI', '-AutoQuit',
               '-OpenTraceFile=' + str(trace), '-ABSLOG=' + str(output / 'Insights.log'),
               '-ExecOnAnalysisCompleteCmd=' + export]
    report = run_record(command, output / 'InsightsRun.json', engine=engine, script=__file__)
    # Insights parses UE's raw Windows command line, rather than CRT argv.
    # Quote each value after '=', not the whole '-option=value' token. No shell.
    windows_command = subprocess.list2cmdline(command[:1]) + ' -NoUI -AutoQuit'
    for key, value in [('OpenTraceFile', str(trace)), ('ABSLOG', str(output / 'Insights.log')),
                       ('ExecOnAnalysisCompleteCmd', export)]:
        assert not any(character in value for character in ['"', '\r', '\n']), 'Invalid command value'
        windows_command += f' -{key}="{value}"'
    report['windows_command_line'] = windows_command
    report['trace_sha256'] = hashlib.sha256(trace.read_bytes()).hexdigest()
    report['trace_bytes'] = trace.stat().st_size
    with (output / 'InsightsProcess.log').open('wb') as log:
        process = subprocess.Popen(windows_command, cwd=output, stdout=log, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            report['exit_code'] = process.wait(timeout=120)
        except subprocess.TimeoutExpired:
            process.kill()
            report['exit_code'] = process.wait(timeout=30)
            report['error'] = 'Insights export timed out'
    if destination.is_file():
        with destination.open(encoding='utf-8-sig', newline='') as stream:
            report['exported_events'] = len(list(csv.DictReader(stream)))
    report['result'] = 'passed' if report['exit_code'] == 0 and report.get('exported_events', 0) > 0 else 'failed'
    (output / 'InsightsRun.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: report.get(key) for key in ['result', 'exit_code', 'exported_events', 'trace_bytes']}))
    return 0 if report['result'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
