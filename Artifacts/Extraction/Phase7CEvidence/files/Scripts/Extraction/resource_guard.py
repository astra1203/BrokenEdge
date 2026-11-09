"""Run one owned task tree with recorded disk/RAM limits; never close shared apps."""
import argparse
import ctypes
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
GIB = 1024 ** 3


class MemoryStatus(ctypes.Structure):
    _fields_ = [('length', ctypes.c_ulong), ('load', ctypes.c_ulong)] + [
        (name, ctypes.c_ulonglong) for name in
        ['physical', 'available', 'commit_limit', 'commit_available', 'virtual', 'virtual_available', 'extended']]


def sample():
    memory = MemoryStatus()
    memory.length = ctypes.sizeof(memory)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
        raise ctypes.WinError()
    return {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'c_free_gib': shutil.disk_usage('C:/').free / GIB,
            'd_free_gib': shutil.disk_usage('D:/').free / GIB,
            'ram_free_gib': memory.available / GIB,
            'commit_used_gib': (memory.commit_limit - memory.commit_available) / GIB,
            'commit_limit_gib': memory.commit_limit / GIB}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    parser.add_argument('--timeout', type=int, default=7200)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
        raise ValueError('Use a new alphanumeric run tag')
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('A child command is required')
    out = ROOT / 'Saved/Extraction' / (args.tag + '-resources')
    out.mkdir(parents=True, exist_ok=False)
    # Prevent this workflow from starting two resource-heavy task groups at once.
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateMutexW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    mutex = kernel.CreateMutexW(None, False, 'Local\\BrokenEdgePhase7Work')
    if not mutex or ctypes.get_last_error() == 183:
        if mutex:
            kernel.CloseHandle(mutex)
        raise RuntimeError('Another guarded BrokenEdge task is running')
    report = {'command': command, 'result': 'running', 'samples': [], 'limits': {
        'start_c_gib': 40, 'start_d_gib': 100, 'start_ram_gib': 8,
        'stop_c_gib': 25, 'stop_d_gib': 60, 'stop_ram_gib': 3, 'low_ram_seconds': 15}}
    process = None

    def save():
        (out / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    try:
        first = sample()
        report['samples'].append(first)
        save()
        if first['c_free_gib'] < 40 or first['d_free_gib'] < 100 or first['ram_free_gib'] < 8:
            raise RuntimeError('Resource preflight failed: ' + str(first))
        environment = os.environ.copy()
        temporary = out / 'Temp'
        temporary.mkdir()
        environment.update(TEMP=str(temporary), TMP=str(temporary), UE_SKIP_UBT_SDK_SETUP='1')
        with (out / 'Task.log').open('wb') as log:
            process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log,
                                       stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            report['pid'] = process.pid
            print('GUARDED_TASK', process.pid, out, flush=True)
            started, low_since = time.monotonic(), None
            while process.poll() is None:
                current, now = sample(), time.monotonic()
                report['samples'].append(current)
                save()
                low_since = (low_since or now) if current['ram_free_gib'] < 3 else None
                if current['c_free_gib'] < 25 or current['d_free_gib'] < 60 or (low_since and now - low_since >= 15):
                    raise RuntimeError('Resource limit reached: ' + str(current))
                if now - started > args.timeout:
                    raise RuntimeError('Guarded task timeout')
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
        report['exit_code'] = process.returncode
        report['result'] = 'passed' if process.returncode == 0 else 'failed'
    except BaseException as error:
        report['result'], report['error'] = 'failed', repr(error)
        if process and process.poll() is None:
            # Only this wrapper's still-live child PID and its descendants are targeted.
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T'], capture_output=True)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
                process.wait(timeout=30)
        raise
    finally:
        report['samples'].append(sample())
        save()
        kernel.CloseHandle(mutex)
        print('GUARDED_RESULT', report['result'], flush=True)
    return report.get('exit_code', 1)


if __name__ == '__main__':
    raise SystemExit(main())
