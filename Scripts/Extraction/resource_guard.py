"""Run one owned task tree with recorded disk/RAM limits; never close shared apps."""
import argparse
import ctypes
import datetime
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
GIB = 1024 ** 3
DEFAULT_LIMITS = {
    'start_c_gib': 40, 'start_d_gib': 100, 'start_ram_gib': 8,
    'stop_c_gib': 25, 'stop_d_gib': 60, 'stop_ram_gib': 3,
    'critical_ram_gib': 1, 'low_ram_seconds': 15,
}


def validate_limits(limits):
    """Reject invalid configuration before launching or terminating any process."""
    for name in DEFAULT_LIMITS:
        value = limits[name]
        if not math.isfinite(value) or value < 0:
            raise ValueError(name + ' must be finite and nonnegative')
    for resource in ('c', 'd', 'ram'):
        if limits['start_' + resource + '_gib'] < limits['stop_' + resource + '_gib']:
            raise ValueError(resource + ': start threshold must be >= stop threshold')
    if limits['critical_ram_gib'] > limits['stop_ram_gib']:
        raise ValueError('critical_ram_gib must be <= stop_ram_gib')


def threshold_reason(current, limits, phase, resource):
    key = resource + '_free_gib'
    limit_key = phase + '_' + resource + '_gib'
    if current[key] < limits[limit_key]:
        return {'code': limit_key, 'resource': key, 'observed_gib': current[key],
                'limit_gib': limits[limit_key], 'comparison': '<'}
    return None


def preflight_reason(current, limits):
    for resource in ('c', 'd', 'ram'):
        reason = threshold_reason(current, limits, 'start', resource)
        if reason:
            return reason
    return None


def runtime_reason(current, limits, now, low_since):
    """Return (low-RAM start time, violation); critical RAM has no grace period.

    All resource thresholds are strict '<'; equality is permitted. A recovered
    RAM sample resets the sustained-low interval, including a start time of 0.
    """
    low_since = (now if low_since is None else low_since) if current['ram_free_gib'] < limits['stop_ram_gib'] else None
    critical = threshold_reason(current, limits, 'critical', 'ram')
    if critical:
        return low_since, critical
    for resource in ('c', 'd'):
        reason = threshold_reason(current, limits, 'stop', resource)
        if reason:
            return low_since, reason
    if low_since is not None and now - low_since >= limits['low_ram_seconds']:
        reason = threshold_reason(current, limits, 'stop', 'ram')
        reason.update(low_ram_seconds=now - low_since, grace_seconds=limits['low_ram_seconds'])
        return low_since, reason
    return low_since, None


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
    for name in ('start_d_gib', 'stop_d_gib', 'start_ram_gib', 'stop_ram_gib', 'critical_ram_gib', 'low_ram_seconds'):
        parser.add_argument('--' + name.replace('_', '-'), '--' + name, dest=name, type=float, default=DEFAULT_LIMITS[name])
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    limits = dict(DEFAULT_LIMITS)
    for name in limits:
        if hasattr(args, name):
            limits[name] = getattr(args, name)
    try:
        validate_limits(limits)
    except ValueError as error:
        parser.error(str(error))
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
    report = {'command': command, 'result': 'running', 'samples': [], 'limits': limits}
    process = None
    low_since = None

    def save():
        (out / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    try:
        first = sample()
        report['samples'].append(first)
        save()
        reason = preflight_reason(first, limits)
        if reason:
            report['trigger'] = reason
            raise RuntimeError('Resource preflight failed: ' + str(reason))
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
                low_since, reason = runtime_reason(current, limits, now, low_since)
                if reason:
                    report['trigger'] = reason
                    raise RuntimeError('Resource limit reached: ' + str(reason))
                if now - started > args.timeout:
                    report['trigger'] = {'code': 'timeout', 'elapsed_seconds': now - started, 'limit_seconds': args.timeout}
                    raise RuntimeError('Guarded task timeout')
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
        report['exit_code'] = process.returncode
        report['result'] = 'passed' if process.returncode == 0 else 'failed'
        if process.returncode != 0:
            report['trigger'] = {'code': 'child_exit', 'exit_code': process.returncode}
    except BaseException as error:
        report['result'], report['error'] = 'failed', repr(error)
        report.setdefault('trigger', {'code': 'exception', 'message': repr(error)})
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
        try:
            final = sample()
            report['samples'].append(final)
            if report['result'] == 'passed':
                _, reason = runtime_reason(final, limits, time.monotonic(), low_since)
                if reason:
                    report['result'], report['trigger'] = 'failed', reason
                    report['error'] = 'Resource limit reached at task completion: ' + str(reason)
        except Exception as error:
            report['result'] = 'failed'
            report.setdefault('trigger', {'code': 'sample_failed', 'message': repr(error)})
            report.setdefault('error', repr(error))
        finally:
            try:
                save()
            finally:
                kernel.CloseHandle(mutex)
                print('GUARDED_RESULT', report['result'], flush=True)
    return report.get('exit_code', 0) if report['result'] == 'passed' else (report.get('exit_code') or 1)


if __name__ == '__main__':
    raise SystemExit(main())
