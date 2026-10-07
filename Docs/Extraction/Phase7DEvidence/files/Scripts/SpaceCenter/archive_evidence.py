"""Archive reviewed phase-1 evidence, with byte hashes and explicit validation boundaries."""
import argparse
import csv
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess
from source_packages import PROJECT, WORK, write_json


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def profile(path):
    with path.open(encoding='utf-8-sig', newline='') as source:
        rows = list(csv.DictReader(source))
    result = {}
    for name in ['FrameTime', 'GameThreadTime', 'RenderThreadTime', 'GPUTime', 'PhysicalUsedMB', 'GPUMem/LocalUsedMB']:
        values = []
        for row in rows:
            try:
                values.append(float(row[name]))
            except (ValueError, KeyError, TypeError):
                pass  # The UE CSV includes a repeated header and a metadata footer.
        values.sort()
        if values:
            result[name] = {'frames': len(values), 'p50': statistics.median(values),
                            'p95': values[int(.95 * (len(values) - 1))], 'maximum': max(values)}
    return result


def main():
    parser = argparse.ArgumentParser()
    for name in ['import', 'audit', 'capture', 'single', 'multi']:
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--conversion', required=True)
    parser.add_argument('--baseline-audit', required=True)
    parser.add_argument('--baseline-actions', required=True)
    parser.add_argument('--output', type=Path, default=PROJECT / 'Docs/SpaceCenter/Phase1Evidence')
    args = parser.parse_args()
    for path in [WORK / 'ConversionRuns' / args.conversion / 'Run.json', WORK / 'GeometryAudit.json', WORK / 'Repeatability.json']:
        if json.loads(path.read_text(encoding='utf-8'))['result'] != 'passed':
            raise ValueError('Prerequisite failed: ' + str(path))
    dest = args.output
    dest.mkdir(parents=True, exist_ok=False)

    def copy(source, target, compressed=False):
        target = dest / target
        target.parent.mkdir(parents=True, exist_ok=True)
        if compressed:
            target.write_bytes(gzip.compress(source.read_bytes(), mtime=0))
        else:
            shutil.copyfile(source, target)

    runs, performance = {}, {}
    for name in ['import', 'audit', 'capture', 'single', 'multi']:
        tag = getattr(args, name)
        folder = WORK / 'Runs' / tag
        report = json.loads((folder / 'Report.json').read_text(encoding='utf-8'))
        run = json.loads((folder / 'Run.json').read_text(encoding='utf-8'))
        if report['result'] != 'passed' or run['exit_code'] != 0:
            raise ValueError('Cannot archive failed run as acceptance: ' + tag)
        runs[name] = {'tag': tag, 'checks': len(report.get('checks', [])), 'result': 'passed'}
        for filename in ['Report.json', 'Run.json']:
            copy(folder / filename, name + '/' + filename)
        copy(folder / 'Editor.log', name + '/Editor.log.gz', True)
        for image in folder.glob('*.png'):
            copy(image, name + '/' + image.name)
        for filename in ['Probes.json', 'BlockedEdges.json']:
            if (folder / filename).is_file():
                copy(folder / filename, name + '/' + filename + '.gz', True)
        if (folder / 'Performance.csv').is_file():
            performance[name] = {'viewport_sizes': report['viewport_sizes'], 'statistics': profile(folder / 'Performance.csv')}
            copy(folder / 'Performance.csv', name + '/Performance.csv.gz', True)
    for name, report_name in [('audit', 'Audit.json'), ('actions', 'pie_validation.json')]:
        tag = getattr(args, 'baseline_' + name)
        folder = PROJECT / 'Saved/Stinger/Baseline' / tag
        report = json.loads((folder / report_name).read_text(encoding='utf-8'))
        run = json.loads((folder / 'Run.json').read_text(encoding='utf-8'))
        if report['result'] != 'passed' or run['exit_code'] != 0:
            raise ValueError('Baseline regression failed: ' + tag)
        runs['baseline-' + name] = {'tag': tag, 'checks': len(report['checks']), 'result': 'passed'}
        for filename in [report_name, 'Run.json']:
            copy(folder / filename, 'baseline-' + name + '/' + filename)
        copy(folder / 'Editor.log', 'baseline-' + name + '/Editor.log.gz', True)
    build = json.loads((WORK / 'Build/Build.json').read_text(encoding='utf-8'))
    if build['exit_code'] != 0:
        raise ValueError('Editor build failed')
    copy(WORK / 'Build/Build.json', 'build/Build.json')
    copy(WORK / 'Build/Build.txt', 'build/Build.raw.log.gz', True)
    readable = []
    for line in (WORK / 'Build/Build.txt').read_bytes().splitlines(keepends=True):
        try:
            readable.append(line.decode('utf-8'))
        except UnicodeDecodeError:
            readable.append(line.decode('gb18030'))
    (dest / 'build/Build.txt').write_text(''.join(readable), encoding='utf-8')
    conversion = WORK / 'ConversionRuns' / args.conversion
    for source in conversion.glob('*'):
        if source.name in ['Selection01.json', 'ImportManifest.json', 'GeometryAudit.json'] or not source.is_file():
            continue
        copy(source, 'conversion/' + source.name + '.gz', True)
    for tag, reason in [('traverse-02', 'One-sided exterior-wall collision blocked the return route; superseded by two-sided collision and the north exit.'),
                        ('traverse-2p-02', 'Test driver matched actor names instead of PlayerState IDs and chose an obstructed waiting point; superseded by the final multiplayer test.')]:
        folder = WORK / 'Runs' / tag
        for filename in ['Report.json', 'Run.json']:
            copy(folder / filename, 'diagnostics/' + tag + '/' + filename)
        write_json(dest / 'diagnostics' / tag / 'Reason.json', {'acceptance': False, 'reason': reason})
    for filename in ['sample_config.json', 'overrides.json', 'waypoints.json', 'route.json']:
        copy(PROJECT / 'Scripts/SpaceCenter' / filename, 'configuration/' + filename)
    manifest = json.loads((WORK / 'ImportManifest.json').read_text(encoding='utf-8'))
    write_json(dest / 'SceneManifest.json', {**{k: manifest[k] for k in ['version', 'origin', 'excluded', 'missing_materials']},
        'actors': [{k: a[k] for k in ['id', 'source_object', 'source_package', 'mesh', 'transform', 'materials']} for a in manifest['actors']]})
    write_json(dest / 'MaterialDecisions.json', manifest['materials'])
    write_json(dest / 'MeshMetadata.json', {p: e['metadata'] for p, e in manifest['meshes'].items()})
    for filename in ['GeometryAudit.json', 'ImportedAssets.json', 'ArchiveIndex.json', 'Repeatability.json', 'Hardware.json', 'VisualReview.json']:
        copy(WORK / filename, filename)
    write_json(dest / 'Performance.json', {'scope': 'Rendered offscreen editor PIE; high scalability (2), screen percentage 100, VSync off. CSV includes route pauses and screenshot capture costs. Not packaged-game or dedicated-server capacity evidence.', 'runs': performance})
    assets = [{'path': str(p.relative_to(PROJECT)).replace('\\', '/'), 'bytes': p.stat().st_size, 'sha256': sha(p)}
              for p in sorted((PROJECT / 'Content/Maps/SpaceCenter').rglob('*')) if p.is_file()]
    write_json(dest / 'GeneratedAssets.json', {'files': assets, 'bytes': sum(a['bytes'] for a in assets)})
    scripts = [{'path': str(p.relative_to(PROJECT)).replace('\\', '/'), 'sha256': sha(p)}
               for p in sorted((PROJECT / 'Scripts/SpaceCenter').rglob('*'))
               if p.is_file() and p.suffix in ['.py', '.json', '.cs', '.csproj', '.ps1', '.md'] and not any(part in ['bin', 'obj', '__pycache__'] for part in p.parts)]
    write_json(dest / 'Implementation.json', scripts)
    files = [{'path': str(p.relative_to(dest)).replace('\\', '/'), 'bytes': p.stat().st_size, 'sha256': sha(p)} for p in sorted(dest.rglob('*')) if p.is_file()]
    write_json(dest / 'Summary.json', {'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=PROJECT, text=True).strip(),
        'runs': runs, 'conversion': args.conversion, 'counts': {k: len(manifest[k]) for k in ['actors', 'meshes', 'materials', 'textures']}, 'files': files})
    print('Archived', len(files), 'files to', dest)


if __name__ == '__main__':
    main()
