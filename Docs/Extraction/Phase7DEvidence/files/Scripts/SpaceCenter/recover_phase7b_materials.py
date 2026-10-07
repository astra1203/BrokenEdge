"""Recover the original material dependency graph without flattening its layers."""
import argparse
import collections
import json
from pathlib import Path
import sqlite3
import struct

from phase7b_source import BASE_WORK, ROOT, TERRAIN, WORK, inspect, sha, write_json
from source_packages import package_key


def canonical(path):
    return path.split('.')[0].replace('DeltaForce/Content/', '/Game/')


def references(value):
    if isinstance(value, dict):
        if isinstance(value.get('ObjectPath'), str):
            yield value
        for child in value.values():
            yield from references(child)
    elif isinstance(value, list):
        for child in value:
            yield from references(child)


def source_array_proof(package, obj, indices):
    stem = BASE_WORK / 'Source/DeltaForce/Content' / package.removeprefix('/Game/')
    asset, export = stem.with_suffix('.uasset'), stem.with_suffix('.uexp')
    data = export.read_bytes()
    metadata = obj['source_export']
    start = metadata['serial_offset'] - asset.stat().st_size
    size = metadata['serial_size']
    if start < 0 or size < 0 or start + size > len(data):
        raise ValueError('Source export outside package: ' + package)
    pattern = struct.pack('<i', len(indices)) + struct.pack('<' + 'i' * len(indices), *indices)
    region = data[start:start + size]
    if region.count(pattern) != 1:
        raise ValueError('Expression index array not independently confirmed in source bytes: ' + package)
    return {'asset_sha256': sha(asset), 'export_sha256': sha(export),
            'array_uexp_offset': start + region.index(pattern), 'array_bytes': len(pattern),
            'verification': 'Count and every signed int32 index match original export bytes, including zero indices.'}


def audit_connectivity(packages, verify_bytes=True):
    """Dependency names do not prove that an executable source graph survived."""
    incomplete = []
    for name, package in sorted(packages.items()):
        objects = package.get('objects', [])
        for obj in objects:
            field = {'Material': 'Expressions', 'MaterialFunction': 'FunctionExpressions'}.get(obj.get('Type'))
            if not field:
                continue
            indices = obj.get(field + 'SourceIndices')
            if indices is None:
                incomplete.append({'package': name, 'object': obj['Name'], 'reason': 'source_indices_not_recorded'})
                continue
            nulls = [i for i, index in enumerate(indices) if index == 0]
            inputs = sum(o.get('Type') == 'MaterialExpressionFunctionInput' for o in objects)
            outputs = sum(o.get('Type') == 'MaterialExpressionFunctionOutput' for o in objects)
            if nulls or (obj['Type'] == 'MaterialFunction' and not outputs):
                incomplete.append({'package': name, 'object': obj['Name'], 'type': obj['Type'],
                                   'filter_editor_only': package.get('filter_editor_only'),
                                   'source_export': obj.get('source_export'), 'expression_slots': len(indices),
                                   'null_source_indices': nulls, 'function_inputs': inputs, 'function_outputs': outputs,
                                   'raw_array_proof': source_array_proof(name, obj, indices) if verify_bytes else None,
                                   'reason': 'source_graph_has_removed_nodes_or_missing_function_outputs'})
    return incomplete


def recover(tag):
    out = WORK / 'MaterialRuns' / tag
    out.mkdir(parents=True, exist_ok=False)
    roots = {TERRAIN + 'MI_Terrain_SpaceCenter_HD_CDLOD'}
    for folder in [BASE_WORK, BASE_WORK / 'Phase7A']:
        manifest = json.loads((folder / 'ImportManifest.json').read_text(encoding='utf-8'))
        roots.update(manifest['materials'])
    pending, packages, missing, native, textures, custom = set(roots), {}, set(), set(), set(), []
    database = sqlite3.connect((BASE_WORK / 'catalog.sqlite').resolve().as_uri() + '?mode=ro', uri=True)
    round_index = 0
    try:
        while pending:
            round_index += 1
            available = []
            for name in sorted(pending):
                if name.startswith('/Engine/'):
                    native.add(name)
                elif database.execute('SELECT 1 FROM entries WHERE package=? LIMIT 1', (package_key(name),)).fetchone():
                    available.append(name)
                else:
                    missing.add(name)
            if not available:
                break
            if len(packages) + len(available) > 2500:
                raise RuntimeError('Material graph exceeds the bounded 2500-package budget')
            batch = out / ('round-' + str(round_index))
            inspect(available, batch)
            parsed = json.loads((batch / 'Parsed/Packages.json').read_text(encoding='utf-8'))
            pending = set()
            for package in parsed:
                name = canonical(package['package'])
                packages[name] = package
                for ref in references(package.get('objects', [])):
                    target = canonical(ref['ObjectPath'])
                    kind = ref.get('ObjectName', '').split("'", 1)[0].split(':')[-1]
                    if kind in {'MaterialFunction', 'MaterialFunctionInstance', 'Material', 'MaterialInstanceConstant',
                                'VirtualMaterialInstanceConstant', 'MaterialParameterCollection'}:
                        if target.startswith(('/Game/', '/Engine/')) and target not in packages and target not in missing:
                            pending.add(target)
                    elif 'Texture' in kind:
                        textures.add(target)
                for obj in package.get('objects', []):
                    if obj.get('Type') == 'MaterialExpressionCustom':
                        custom.append({'package': name, 'object': obj})
            pending.difference_update(packages)
        unresolved_textures = sorted(name for name in textures if name.startswith('/Game/') and not
                                     database.execute('SELECT 1 FROM entries WHERE package=? LIMIT 1',
                                                      (package_key(name),)).fetchone())
    finally:
        database.close()
    failures = [{'package': name, 'error': p.get('error'), 'failures': p.get('failures')}
                for name, p in packages.items() if p.get('error') or p.get('failures')]
    types = collections.Counter(o.get('Type', '?') for p in packages.values() for o in p.get('objects', []))
    connectivity = audit_connectivity(packages)
    report = {'result': 'source_gate_passed' if not missing and not failures and not connectivity and not unresolved_textures else 'blocked',
              'dependency_parse': 'passed' if not missing and not failures else 'failed',
              'roots': sorted(roots), 'packages': len(packages), 'objects': sum(types.values()),
              'types': dict(sorted(types.items())), 'missing_graph_packages': sorted(missing),
              'native_engine_references': sorted(native), 'texture_packages': sorted(textures),
              'missing_texture_packages': unresolved_textures, 'parse_failures': failures,
              'custom_expressions': len(custom),
              'incomplete_graphs': connectivity,
              'boundary': 'Dependency recovery is not shader recovery. Zero source indices and stripped inputs cannot be reconstructed by guessing. Native import and rendering remain unvalidated.'}
    write_json(out / 'Graph.json', dict(sorted(packages.items())))
    write_json(out / 'CustomExpressions.json', custom)
    write_json(out / 'Report.json', report)
    print(json.dumps({k: v for k, v in report.items() if k not in {'roots', 'types', 'native_engine_references', 'texture_packages', 'incomplete_graphs'}}
                     | {'incomplete_graphs': len(connectivity)}))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
        parser.error('Use a new alphanumeric run tag')
    raise SystemExit(0 if recover(args.tag)['result'] == 'source_gate_passed' else 2)
