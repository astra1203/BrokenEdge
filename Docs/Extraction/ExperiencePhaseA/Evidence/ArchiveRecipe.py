"""One-shot explicit evidence recipe; excludes stores, staged/cooked output and credentials."""
from pathlib import Path
import datetime
import gzip
import hashlib
import json
import subprocess

ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'BrokenEdge.uproject').is_file())
OUT = ROOT / 'Docs/Extraction/ExperiencePhaseA/Evidence'
TAGS = [
    'ExperienceAFileServer01', 'ExperienceAFileServer02', 'ExperienceAFileServer03',
    'ExperienceAFileClient01', 'ExperienceASettlement01', 'ExperienceASettlement02',
    'ExperienceARecovery01', 'ExperienceAGuards01', 'ExperienceAGuards02',
    'ExperienceAGuardsDiag01', 'ExperienceAGuardsDiag02',
    'ExperienceAReference03', 'ExperienceAGateTests02', 'ExperienceASources02',
]


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def rel(path, base=ROOT):
    return path.relative_to(base).as_posix()


def write(path, value):
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def main():
    OUT.mkdir(parents=True, exist_ok=False)
    records, runs = [], []

    def preserve(source, destination):
        data = source.read_bytes()
        compressed = source.suffix == '.log' or (source.suffix == '.json' and len(data) > 1024 * 1024)
        if compressed:
            destination = destination.with_suffix(destination.suffix + '.gz')
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open('xb') as stream:
            stream.write(gzip.compress(data, mtime=0) if compressed else data)
        restored = gzip.decompress(destination.read_bytes()) if compressed else destination.read_bytes()
        assert restored == data, destination
        records.append(dict(source=rel(source), archive=rel(destination, OUT), source_size=len(data),
                            source_sha256=hashlib.sha256(data).hexdigest(), archive_sha256=sha(destination)))

    for tag in TAGS:
        folder = ROOT / 'Saved/Extraction' / tag
        report_path = folder / ('Run.json' if 'File' in tag else 'Report.json')
        info = {'tag': tag}
        if report_path.exists():
            report = read(report_path)
            info.update(result=report.get('result', 'passed' if report.get('exit_code') == 0 else 'failed'),
                        checks=len(report.get('checks', [])), error=report.get('error'),
                        exit_code=report.get('exit_code'))
            if info['result'] == 'running':
                assert tag == 'ExperienceAGuardsDiag01', tag
                info['result'] = 'aborted_diagnostic'
            if 'guard_tuning' in report:
                info['guard_tuning'] = report['guard_tuning']
            for name in ['Run.json', 'Report.json', 'DefinitionPointers.json', 'DebuggerEvidence.json']:
                if (folder / name).is_file():
                    preserve(folder / name, OUT / tag / name)
            for file in sorted(folder.glob('*.log')):
                preserve(file, OUT / tag / file.name)
        resources = folder.with_name(tag + '-resources')
        if (resources / 'Report.json').exists():
            info['resource_result'] = read(resources / 'Report.json')['result']
            for name in ['Report.json', 'Task.log']:
                if (resources / name).is_file():
                    preserve(resources / name, OUT / tag / 'resources' / name)
        runs.append(info)
    for name in ['A-extraction-countdown.png', 'A-stash-overflow.png']:
        preserve(ROOT / 'Saved/Extraction/ExperienceASettlement02' / name, OUT / 'captures' / name)
    preserve(Path(__file__), OUT / 'ArchiveRecipe.py')
    packages = {rel(p): {'size': p.stat().st_size, 'sha256': sha(p)}
                for p in sorted((ROOT / 'Packaged/ExperiencePhaseA03').rglob('*'))
                if p.is_file() and p.suffix.lower() in ('.exe', '.pak', '.ucas', '.utoc')}
    assert packages and any('/Client/' in p for p in packages) and any('/Server/' in p for p in packages)
    write(OUT / 'PackageManifest.json', packages)
    changed = subprocess.check_output(['git', '-c', 'core.quotePath=false', 'diff', '--name-only'], cwd=ROOT).decode('utf-8').splitlines()
    changed += subprocess.check_output(['git', '-c', 'core.quotePath=false', 'ls-files', '--others', '--exclude-standard'], cwd=ROOT).decode('utf-8').splitlines()
    implementation = {p: sha(ROOT / p) for p in sorted(set(changed))
                      if p.startswith(('Scripts/', 'Source/')) and (ROOT / p).is_file()}
    write(OUT / 'Summary.json', {
        'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'phase_a_complete': False, 'phase_b_started': False,
        'reference_gate': read(ROOT / 'Saved/Extraction/ExperienceAReference03/Report.json'),
        'runs': runs, 'implementation_sha256': implementation,
        'boundaries': ['Reference version and all 103 observations remain missing.',
                       'Source inventory and GLB structure do not prove complete imported weapons.',
                       'Two-rendered guard acquisition timeout remains unresolved; keep failed evidence.',
                       'Debugger run was interrupted; subsequent resource preflight failed before launch.',
                       'No new traversal, weak-network, audio, recoil, UI-reference or performance acceptance.',
                       'Original play server and package were preserved. No commit or push.'],
    })
    write(OUT / 'SourceArchiveIndex.json', records)
    write(OUT / 'ArchiveIndex.json', {rel(p, OUT): {'size': p.stat().st_size, 'sha256': sha(p)}
                                    for p in sorted(OUT.rglob('*')) if p.is_file()})
    print('ARCHIVED', len(records), 'source files;', len(packages), 'package payload hashes')


if __name__ == '__main__':
    main()
