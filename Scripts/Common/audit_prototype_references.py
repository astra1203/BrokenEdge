"""Read-only native prototype audit: parent tags, imports, soft paths and serialized literals."""
import hashlib
import json
import os
from pathlib import Path
import unreal

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
names = ('MyCharacter', 'CombatObserver')
targets = ['/Script/BrokenEdge.' + name for name in names]
report = {'classes': {}, 'module_referencers': sorted(map(str, registry.get_referencers('/Script/BrokenEdge', unreal.AssetRegistryDependencyOptions())))}
assets = registry.get_all_assets()
for name in names:
    target = '/Script/BrokenEdge.' + name
    matches = []
    for asset in assets:
        tags = {key: str(asset.get_tag_value(key) or '') for key in ('ParentClass', 'NativeParentClass')}
        if any(target in value for value in tags.values()):
            loaded = asset.get_asset()
            matches.append({'package': str(asset.package_name), 'tags': tags, 'loaded': loaded is not None})
    report['classes'][name] = {'blueprint_parents': matches}
out = Path(os.environ['EXTRACTION_RUN_DIR'])
root = Path(unreal.Paths.project_dir()).resolve()
# Check a known positive before accepting any negative scan result.
control_path = root / 'Content/Blueprints/Character/BP_BrokenEdgeCharacter.uasset'
control = json.loads(unreal.NativeReferenceAudit.inspect_package(str(control_path), ['/Script/BrokenEdge.BrokenEdgeCharacter']))
assert control['read'] and '/Script/BrokenEdge.BrokenEdgeCharacter' in control['hard_references'], control
report['positive_control'] = control
roots = [root / 'Content'] + sorted((root / 'Plugins').glob('*/Content'))
files = sorted(path for folder in roots for path in folder.rglob('*') if path.suffix in ('.uasset', '.umap'))
scanned, failures, references = [], [], []
patterns = {target: (target.encode('utf-8'), target.encode('utf-16-le')) for target in targets}
for index, path in enumerate(files):
    result = json.loads(unreal.NativeReferenceAudit.inspect_package(str(path), targets))
    result['file'] = path.relative_to(root).as_posix()
    # Includes pre-soft-path-table packages and arbitrary serialized path strings.
    # Bare short names are not identities: third-party blueprints also use "MyCharacter".
    digest, tail, literal_targets = hashlib.sha256(), b'', set()
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
            data = tail + chunk
            for target, encodings in patterns.items():
                if any(pattern in data for pattern in encodings): literal_targets.add(target)
            tail = data[-256:]
    result['sha256'] = digest.hexdigest()
    result['serialized_path_literals'] = sorted(literal_targets)
    scanned.append(result)
    if not result['read']: failures.append(result['file'])
    elif any(result[key] for key in ('hard_references', 'soft_references', 'path_names', 'serialized_path_literals')):
        references.append(result)
    if index % 100 == 99:
        unreal.SystemLibrary.collect_garbage()
        print('PROTOTYPE_SCAN', index + 1, '/', len(files))
(out / 'PackageScan.json').write_text(json.dumps(scanned, ensure_ascii=False, indent=2), encoding='utf-8')
report.update(scope='All project Content and direct project-plugin Content packages: Blueprint parent tags, serialized native imports (including graph/property types), soft-object-path tables, name-table paths and UTF-8/UTF-16 path literals. Runtime-constructed names and external content are outside this audit.',
              scanned_packages=len(scanned), failed_packages=failures, references=references,
              package_scan_sha256=hashlib.sha256((out / 'PackageScan.json').read_bytes()).hexdigest())
report['unreferenced'] = not failures and not references and all(not entry['blueprint_parents'] for entry in report['classes'].values())
(out / 'PrototypeReferences.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
assert not failures, f'{len(failures)} unreadable packages; audit incomplete'
print('PROTOTYPE_REFERENCE_AUDIT_COMPLETE')
