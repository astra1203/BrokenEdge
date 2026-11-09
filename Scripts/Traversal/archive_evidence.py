"""Archive passing package acceptance with byte-preserving copies and hashes."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts/Extraction'))
from pe_validation import validate_executable

parser = argparse.ArgumentParser()
parser.add_argument('--server-build', required=True)
parser.add_argument('--client-build', required=True)
parser.add_argument('--runs', nargs='+', required=True)
parser.add_argument('--video-runs', nargs='+', required=True)
args = parser.parse_args()
OUT = ROOT / 'Artifacts/Extraction/Traversal/Evidence'
OUT.mkdir(parents=True, exist_ok=True)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run_folder(tag):
    assert tag and all(c.isalnum() or c in '-_' for c in tag)
    return ROOT / 'Saved/Extraction' / tag


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


manifest = {'result': 'passed', 'builds': {}, 'runs': {}, 'videos': [], 'files': {}}


def preserve(source, destination, compress=False):
    destination.parent.mkdir(parents=True, exist_ok=True)
    original_hash = digest(source)
    if compress:
        with source.open('rb') as src, destination.open('wb') as dst:
            with gzip.GzipFile(filename='', mode='wb', fileobj=dst, mtime=0) as zipped:
                shutil.copyfileobj(src, zipped)
        with gzip.open(destination, 'rb') as stream:
            assert hashlib.file_digest(stream, 'sha256').hexdigest() == original_hash
    else:
        shutil.copyfile(source, destination)
        assert digest(destination) == original_hash
    manifest['files'][destination.relative_to(OUT).as_posix()] = {
        'source': source.relative_to(ROOT).as_posix(), 'source_sha256': original_hash,
        'sha256': digest(destination), 'bytes': destination.stat().st_size}


expected = {}
for role, tag in [('Server', args.server_build), ('Client', args.client_build)]:
    folder = run_folder(tag)
    build = read(folder / 'Run.json')
    assert build['exit_code'] == 0
    actual = validate_executable(Path(build['executable']['path']))
    assert actual['sha256'] == build['executable']['sha256']
    expected[role] = actual['sha256']
    # A passing executable must still correspond to the current gameplay source and map.
    for name, sha in build['input_sha256'].items():
        if name.startswith(('Source/BrokenEdge/', 'Content/Maps/')):
            assert digest(ROOT / name) == sha, name
    manifest['builds'][role] = {'tag': tag, 'executable': actual}
    preserve(folder / 'Run.json', OUT / 'Builds' / tag / 'Run.json')
    preserve(folder / 'Build.log', OUT / 'Builds' / tag / 'Build.log.gz', True)
    resources = run_folder(tag + '-resources') / 'Report.json'
    assert read(resources)['result'] == 'passed'
    preserve(resources, OUT / 'Builds' / tag / 'Resources.json')

for tag in args.runs:
    folder = run_folder(tag)
    report = read(folder / 'Report.json')
    assert report['result'] == 'passed', tag
    for role, executable in report['executables'].items():
        assert executable['sha256'] == expected['Server' if role == 'Server' else 'Client'], (tag, role)
    resources = run_folder(tag + '-resources') / 'Report.json'
    assert read(resources)['result'] == 'passed', tag
    manifest['runs'][tag] = {'result': 'passed', 'checks': len(report['checks'])}
    for source in folder.glob('*.json'):
        if source.name.endswith(('-state.json', '-command.json')):
            continue
        preserve(source, OUT / 'Runs' / tag / source.name)
    preserve(resources, OUT / 'Runs' / tag / 'Resources.json')
    for source in list(folder.glob('*.log')) + list(folder.glob('*.jsonl')):
        preserve(source, OUT / 'Runs' / tag / (source.name + '.gz'), True)

for tag in args.video_runs:
    assert tag in args.runs
    folder = run_folder(tag)
    for source in sorted(folder.glob('*.mp4')):
        assert not source.name.endswith('.partial.mp4')
        report = read(source.with_suffix('.json'))
        assert report['result'] == 'passed' and digest(source) == report['sha256']
        destination = OUT / 'Videos' / tag / source.name
        preserve(source, destination)
        preserve(source.with_suffix('.json'), destination.with_suffix('.json'))
        preserve(source.with_suffix('') / 'Frames.csv', destination.with_suffix('.frames.csv'))
        manifest['videos'].append({'file': destination.relative_to(OUT).as_posix(),
                                  'seconds': report['seconds'], 'measured_fps': report['measured_fps']})

assert len(manifest['videos']) == 16, 'Expected ten fixture and six formal-route clips'
manifest['checks_including_repeated_runs'] = sum(row['checks'] for row in manifest['runs'].values())
path = OUT / 'Manifest.json'
path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
assert read(path) == manifest
print(json.dumps({'result': manifest['result'], 'runs': len(manifest['runs']),
                  'checks': manifest['checks_including_repeated_runs'], 'videos': len(manifest['videos']),
                  'files': len(manifest['files'])}), flush=True)
