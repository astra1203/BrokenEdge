"""Reproduce this bounded sample from ZIPs. Working cache is replaceable; run reports are tagged."""
import argparse
import collections
import json
from pathlib import Path
import shutil
import subprocess
import sys
from source_packages import PROJECT, WORK, extract, index_archives, write_json
from prepare_sample import canonical, prepare

CONFIG = json.loads((Path(__file__).parent / 'sample_config.json').read_text(encoding='utf-8'))


def run(tag):
    if not tag or not all(c.isalnum() or c in '-_' for c in tag):
        raise ValueError('Tag must be a directory name')
    archive = WORK / 'ConversionRuns' / tag
    archive.mkdir(parents=True, exist_ok=False)
    dotnet = WORK / 'Tools/dotnet/dotnet.exe'
    converter = Path(__file__).parent / 'Converter/bin/Release/net10.0/SpaceCenter.Converter.dll'
    if not dotnet.is_file() or not converter.is_file():
        raise RuntimeError('Run bootstrap.ps1 first')
    runs = []
    extractions = []

    def extract_recorded(packages, recursive=False):
        result = extract(packages, recursive=recursive)
        extractions.append(result)
        write_json(archive / 'SourceExtractions.json', extractions)

    def convert(mode, packages, folder, recovery=(), allow_partial=False):
        request = {'mode': mode, 'source': str(WORK / 'Source/DeltaForce'), 'packages': packages,
                   'output': str(WORK / folder), 'geometry_recovery': list(recovery)}
        request_path = archive / (folder + '-request.json')
        write_json(request_path, request)
        with (archive / (folder + '.log')).open('w', encoding='utf-8') as log:
            result = subprocess.run([str(dotnet), str(converter), str(request_path)], stdout=log, stderr=subprocess.STDOUT)
        runs.append({'mode': mode, 'folder': folder, 'exit_code': result.returncode, 'partial_allowed': allow_partial})
        for name in ['Packages.json', 'ExportResults.json']:
            path = WORK / folder / name
            if path.is_file():
                shutil.copyfile(path, archive / (folder + '-' + name))
        write_json(archive / 'Run.json', {'config': CONFIG, 'stages': runs})
        if result.returncode and not allow_partial:
            raise RuntimeError('Conversion failed: ' + folder + '; inspect ' + str(archive))
        return json.loads((WORK / folder / 'Packages.json').read_text(encoding='utf-8'))

    index_archives(Path(CONFIG['source']))
    extract_recorded(CONFIG['packages'])
    layout = convert('layout', CONFIG['packages'], 'Layout01')
    selected, excluded = [], collections.Counter()
    for package in layout:
        for obj in package['objects']:
            mesh = obj.get('mesh')
            point = obj.get('transform', {}).get('Translation', {})
            if not isinstance(mesh, dict) or not mesh.get('ObjectPath'):
                excluded['no_resolved_mesh'] += 1
                continue
            if not all(CONFIG['min_xy'][i] <= point[k] <= CONFIG['max_xy'][i] for i, k in enumerate(['X', 'Y'])):
                excluded['outside_region'] += 1
                continue
            if '/GenDecalDir/' in mesh['ObjectPath']:
                excluded['generated_decal'] += 1
                continue
            if obj.get('properties', {}).get('bVisible', True) is False:
                excluded['hidden'] += 1
                continue
            obj['source_package'] = package['package']
            selected.append(obj)
    meshes = sorted({canonical(obj['mesh']['ObjectPath']) for obj in selected})
    write_json(WORK / 'Selection01.json', {'origin': CONFIG['origin'], 'actors': selected, 'meshes': meshes, 'excluded': dict(excluded)})
    extract_recorded(meshes)
    metadata = convert('export', meshes, 'Converted01', allow_partial=True)
    recovered = convert('export', CONFIG['geometry_recovery'], 'Recovered01', recovery=CONFIG['geometry_recovery'])
    materials = set()
    for package in metadata + recovered:
        for obj in package['objects']:
            for slot in obj.get('materials', []):
                if slot.get('MaterialInterface'):
                    materials.add(canonical(slot['MaterialInterface']['ObjectPath']))
    for obj in selected:
        for slot in obj.get('properties', {}).get('OverrideMaterials', []) or []:
            if slot and '/Script/' not in slot['ObjectPath']:
                materials.add(canonical(slot['ObjectPath']))
    materials = sorted(p for p in materials if p.startswith('/Game/'))
    extract_recorded(materials, recursive=True)
    parsed_materials = convert('inspect', materials, 'Materials01')
    textures = sorted({canonical(t['ParameterValue']['ObjectPath'])
                       for package in parsed_materials for obj in package['objects']
                       for t in obj.get('Properties', {}).get('TextureParameterValues', []) if t.get('ParameterValue')})
    # Explicit known failures stay in ExportResults and the material approximation report.
    convert('export', textures, 'Textures01', allow_partial=True)
    prepare()
    from audit_geometry import audit
    if audit():
        raise RuntimeError('Geometry audit failed')
    for name in ['ArchiveIndex.json', 'Selection01.json', 'ImportManifest.json', 'GeometryAudit.json']:
        shutil.copyfile(WORK / name, archive / name)
    write_json(archive / 'Run.json', {'result': 'passed', 'config': CONFIG, 'stages': runs,
                                    'boundary': 'Conversion and geometry audit only; UE import and runtime acceptance are separate'})
    print('Conversion complete; run_editor.py import performs UE import:', archive)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    run(parser.parse_args().tag)
