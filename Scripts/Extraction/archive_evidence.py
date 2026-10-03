"""Archive reviewed Extraction runs without changing earlier acceptance evidence."""
import argparse
import datetime
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(file):
    digest = hashlib.sha256()
    with file.open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def read(file):
    return json.loads(file.read_text(encoding='utf-8-sig'))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--phase', type=int, choices=[2, 3, 4], default=2)
    parser.add_argument('--suffix', default='', help='Alphanumeric label for a separate repair archive')
    args = parser.parse_args()
    if args.suffix and not args.suffix.isalnum():
        raise ValueError('Use an alphanumeric archive suffix')
    plan = read(args.plan)
    out = ROOT / 'Docs/Extraction' / f'Phase{args.phase}{args.suffix}Evidence'
    # Validate every entry before creating the immutable evidence directory.
    for entry in plan['runs']:
        folder = ROOT / entry['folder']
        report = read(folder / entry['report'])
        if entry.get('build'):
            assert report['exit_code'] == 0, entry
        else:
            assert report['result'] == 'passed', entry
        if entry.get('metadata'):
            assert read(folder / entry['metadata'])['exit_code'] == 0, entry
    out.mkdir(parents=True, exist_ok=False)
    for entry in plan['runs']:
        folder = ROOT / entry['folder']
        target = out / entry['name']
        for source in folder.rglob('*'):
            if not source.is_file() or source.suffix.lower() not in ['.json', '.log', '.png', '.csv']:
                continue
            # Commands and temporary snapshots are included in the full report.
            if source.name.endswith(('-command.json', '-state.json')):
                continue
            destination = target / source.relative_to(folder)
            destination.parent.mkdir(parents=True, exist_ok=True)
            if source.suffix.lower() in ['.log', '.csv']:
                destination.with_suffix(destination.suffix + '.gz').write_bytes(gzip.compress(source.read_bytes(), mtime=0))
            else:
                shutil.copyfile(source, destination)
    for item in plan.get('extra_files', []):
        source = ROOT / item
        destination = out / 'diagnostics' / source.name
        destination.parent.mkdir(exist_ok=True)
        shutil.copyfile(source, destination)
    for item in plan.get('diagnostic_logs', []):
        source = ROOT / item
        destination = out / 'diagnostics' / (source.parent.name + '-' + source.name + '.gz')
        destination.parent.mkdir(exist_ok=True)
        destination.write_bytes(gzip.compress(source.read_bytes(), mtime=0))
    paths = set(subprocess.check_output(['git', '-c', 'core.quotePath=false', 'diff', '--name-only'], cwd=ROOT).decode('utf-8').splitlines())
    paths.update(subprocess.check_output(['git', '-c', 'core.quotePath=false', 'ls-files', '--others', '--exclude-standard'], cwd=ROOT).decode('utf-8').splitlines())
    paths.update(plan.get('asset_paths', []))
    implementation = {p: sha(ROOT / p) for p in sorted(paths) if (ROOT / p).is_file()
                      and not p.startswith(('Docs/', 'Saved/', 'Packaged/'))}
    packages = {}
    for target in ['Client', 'Server']:
        for file in (ROOT / 'Packaged' / target).rglob('*'):
            if file.is_file() and file.suffix in ['.exe', '.pak', '.ucas', '.utoc']:
                packages[str(file.relative_to(ROOT)).replace('\\', '/')] = {'size': file.stat().st_size, 'sha256': sha(file)}
    summary = {'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
               'engine': 'C:/UnrealProjects/UnrealEngine-5.8.3-release', 'runs': plan['runs'],
               'implementation_sha256': implementation, 'packages': packages,
               'review': plan['review']}
    (out / 'Summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    index = {str(p.relative_to(out)).replace('\\', '/'): {'size': p.stat().st_size, 'sha256': sha(p)}
             for p in sorted(out.rglob('*')) if p.is_file()}
    (out / 'ArchiveIndex.json').write_text(json.dumps(index, indent=2) + '\n', encoding='utf-8')
    print('ARCHIVED', len(index), 'files', out)


if __name__ == '__main__':
    main()
