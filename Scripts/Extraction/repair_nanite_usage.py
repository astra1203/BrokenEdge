"""Enable only the 12 audited Nanite MIC usage overrides, or verify a prior run.

Invoke through run_nanite_usage.py in a fresh UE 5.8 commandlet. Importing this
module outside UE is safe for pure manifest/backup tests. No map is loaded and
only changed allowlisted MICs are saved. A failed save can leave a partial run;
Report.json and path-preserving backups identify exactly what was written.

API evidence (local UE 5.8 source): MaterialEditingLibrary.h:194-223;
MaterialInstance.h:647, 772-821; MaterialInterface.h:76, 655;
PyGenUtil.cpp:GetEnumEntryPythonName; PyWrapperStruct.cpp:export_text.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import traceback

PREFIX = '/Game/Maps/SpaceCenter/Assets/'
EXPECTED_PARENT = '/InterchangeAssets/gltf/MaterialInstances/MI_Default_Opaque.MI_Default_Opaque'
EXPECTED_BASE = '/InterchangeAssets/gltf/M_Default.M_Default'
# Independent of the editable manifest: additions, omissions, duplicates and
# replacing a material with its identically named neighbour must fail closed.
GROUPS = {
    '0f02cdffc1c62311/SM_H_Small_Door_11': (
        'MI_H_Small_Door_11_decal_01A_HD', 'MI_H_Small_Door_11_HD',
        'MI_H_Small_Door_11_Black_HD', 'MI_H_Building_FutureTech_AutomaticWorkshop_01_Decal_HD'),
    '7a44c52b93b3be9a/SM_H_Customiz_ExperimentalDoor_01_L': (
        'MI_H_Customiz_ExperimentalDoor_01A_HD', 'MI_H_Building_FutureTech_AutomaticWorkshop_01_Decal_HD'),
    '409e475f1d6c58b5/SM_H_FutureTech_Tools_Box_01': (
        'MI_H_FutureTech_Tools_Box_01B_HD', 'MI_H_FutureTech_Tools_Box_01_HD',
        'MI_H_Building_FutureTech_AutomaticWorkshop_02_Decal_HD'),
    '056a6189cab1a746/SM_H_Building_FutureTech_HBoss_01_InterWall_03E': (
        'MI_GEN_Metal_Painting_05_HD', 'MI_GEN_Metal_Base_01_HD'),
    '15fd3e1b7cdc4420/SM_H_FutureTech_Furniture_Folder_01_A': (
        'MI_H_FutureTech_Furniture_Folder_01_HD',),
}
ALLOWLIST = frozenset(PREFIX + group + '/Materials/' + name
                     for group, names in GROUPS.items() for name in names)
PARAMETER_ARRAYS = (
    'scalar_parameter_values', 'vector_parameter_values', 'double_vector_parameter_values',
    'texture_parameter_values', 'texture_collection_parameter_values',
    'parameter_collection_parameter_values', 'runtime_virtual_texture_parameter_values',
    'sparse_volume_texture_parameter_values', 'font_parameter_values',
)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def validate_manifest(data):
    packages = [entry['package'] for entry in data['assets']]
    if (data.get('schema_version') != 1 or len(packages) != 12
            or len(set(packages)) != 12 or set(packages) != ALLOWLIST):
        raise ValueError('Manifest must contain exactly the fixed 12 distinct MIC packages')
    if data.get('expected_parent') != EXPECTED_PARENT or data.get('expected_base') != EXPECTED_BASE:
        raise ValueError('Manifest parent/base differs from the audited Interchange chain')
    return sorted(packages)


def asset_relative_path(package):
    if package not in ALLOWLIST:
        raise ValueError('Asset is outside the fixed Nanite allowlist: ' + package)
    return Path('Content') / (package.removeprefix('/Game/') + '.uasset')


def backup_asset(root, out, package, expected_sha):
    relative = asset_relative_path(package)
    source = root / relative
    target = out / 'Backup' / relative
    if sha(source) != expected_sha:
        raise RuntimeError('Asset changed after preflight: ' + package)
    target.parent.mkdir(parents=True, exist_ok=True)
    with source.open('rb') as src, target.open('xb') as dst:
        shutil.copyfileobj(src, dst)
    if sha(target) != expected_sha or sha(source) != expected_sha:
        raise RuntimeError('Backup differs from preflight source: ' + package)
    return target.relative_to(out).as_posix()


def non_usage_overrides(text):
    # UE FStruct export uses the original C++ member names, not Python aliases.
    value = re.sub(r'\b(?:bOverride_UsageFlags|UsageFlags)=[0-9]+,?', '', text)
    value = value.replace(',)', ')')
    if re.search(r'\b(?:bOverride_UsageFlags|UsageFlags)=', value):
        raise ValueError('Unexpected usage flag export format: ' + text)
    return value


def preflight_all(packages, inspect):
    states, errors = {}, []
    for package in packages:
        try:
            states[package] = inspect(package)
        except Exception as error:
            errors.append(dict(package=package, error=f'{type(error).__name__}: {error}'))
    return states, errors


def payload(state):
    return {key: value for key, value in state.items()
            if key not in ('sha256', 'nanite_enabled', 'nanite_override', 'all_usage')}


def check_only_nanite_changed(before, after):
    if payload(before) != payload(after):
        raise RuntimeError('Parent, material parameters or non-usage overrides changed')
    old = {k: v for k, v in before['all_usage'].items() if k != 'MATUSAGE_NANITE'}
    new = {k: v for k, v in after['all_usage'].items() if k != 'MATUSAGE_NANITE'}
    if old != new:
        raise RuntimeError('A usage other than Nanite changed')
    if not after['nanite_enabled'] or not after['nanite_override']:
        raise RuntimeError('Nanite usage override was not enabled')


def execute(unreal, root, engine, out, mode, baseline_file):
    manifest_file = root / 'Scripts/Extraction/nanite_usage_manifest.json'
    manifest = json.loads(manifest_file.read_text(encoding='utf-8-sig'))
    packages = validate_manifest(manifest)
    report = dict(result='running', mode=mode, phase='preflight', manifest_sha256=sha(manifest_file),
                  manifest=manifest, assets=[], errors=[], saved_packages=[])
    report_file = out / 'Report.json'

    def write_report():
        report_file.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    write_report()
    try:
        edit, lib = unreal.MaterialEditingLibrary, unreal.EditorAssetLibrary
        for method in ('has_material_usage', 'has_material_usage_override', 'set_material_usage_override',
                       'get_static_switch_parameter_names', 'get_material_instance_static_switch_parameter_value'):
            if not callable(getattr(edit, method, None)):
                raise RuntimeError('Required UE Python API is unavailable: MaterialEditingLibrary.' + method)
        usage = unreal.MaterialUsage.MATUSAGE_NANITE
        usage_members = {name: getattr(unreal.MaterialUsage, name) for name in dir(unreal.MaterialUsage)
                         if name.startswith('MATUSAGE_') and name != 'MATUSAGE_MAX'}
        if 'MATUSAGE_NANITE' not in usage_members:
            raise RuntimeError('MaterialUsage enumeration is not exposed as expected')
        assets = {}

        def inspect(package):
            file = root / asset_relative_path(package)
            with file.open('rb') as stream:
                if stream.read(128).startswith(b'version https://git-lfs.github.com/spec/v1'):
                    raise RuntimeError('Source asset is an unresolved LFS pointer')
            material = assets.get(package)
            if material is None:
                material = lib.load_asset(package)
                if material is None or material.get_class().get_path_name() != '/Script/Engine.MaterialInstanceConstant':
                    raise RuntimeError('Expected an existing MaterialInstanceConstant')
                assets[package] = material
            parent = material.get_editor_property('parent')
            base = material.get_base_material()
            if parent is None or parent.get_path_name() != EXPECTED_PARENT:
                raise RuntimeError('Unexpected direct Parent')
            if base is None or base.get_path_name() != EXPECTED_BASE:
                raise RuntimeError('Unexpected base material')
            if parent.get_editor_property('parent') != base:
                raise RuntimeError('Unexpected intermediate parent chain')
            domain = base.get_editor_property('material_domain')
            if domain != unreal.MaterialDomain.MD_SURFACE:
                raise RuntimeError('Nanite repair requires a Surface material')
            blend = base.get_editor_property('blend_mode')
            for instance in (parent, material):
                overrides = instance.get_editor_property('base_property_overrides')
                if overrides.get_editor_property('override_blend_mode'):
                    blend = overrides.get_editor_property('blend_mode')
            if blend not in (unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED):
                raise RuntimeError('Unsupported effective blend mode: ' + str(blend))
            parameters = {name: [value.export_text() for value in material.get_editor_property(name)]
                          for name in PARAMETER_ARRAYS}
            parameters['static_switch_effective'] = {
                str(name): bool(edit.get_material_instance_static_switch_parameter_value(material, name))
                for name in edit.get_static_switch_parameter_names(material)}
            overrides = material.get_editor_property('base_property_overrides').export_text()
            return dict(sha256=sha(file), parent=parent.get_path_name(), base=base.get_path_name(),
                        material_class=material.get_class().get_path_name(), domain=str(domain), blend=str(blend),
                        nanite_enabled=bool(edit.has_material_usage(material, usage)),
                        nanite_override=bool(edit.has_material_usage_override(material, usage)),
                        all_usage={name: bool(edit.has_material_usage(material, value))
                                   for name, value in sorted(usage_members.items())},
                        parameters=parameters, non_usage_overrides=non_usage_overrides(overrides),
                        nanite_override_material=material.get_editor_property('nanite_override_material').export_text())

        before, errors = preflight_all(packages, inspect)
        report['assets'] = [dict(package=package, before=before.get(package)) for package in packages]
        report['errors'] = errors
        if errors:
            raise RuntimeError(f'Preflight failed for {len(errors)} assets; nothing was mutated or saved')
        # Protect the common engine parents and the five source meshes as well.
        protected = {'engine_parent': engine / 'Engine/Plugins/Interchange/Assets/Content/gltf/MaterialInstances/MI_Default_Opaque.uasset',
                     'engine_base': engine / 'Engine/Plugins/Interchange/Assets/Content/gltf/M_Default.uasset'}
        for group in GROUPS:
            name = group.rsplit('/', 1)[1]
            protected['mesh:' + group] = root / 'Content/Maps/SpaceCenter/Assets' / group / 'StaticMeshes' / (name + '.uasset')
        protected_before = {name: sha(file) for name, file in protected.items()}
        report['protected_sha256_before'] = protected_before
        baseline = None
        if mode == 'verify':
            baseline = json.loads(baseline_file.read_text(encoding='utf-8-sig'))
            if (baseline.get('result') != 'passed' or baseline.get('mode') != 'repair'
                    or baseline.get('manifest_sha256') != report['manifest_sha256']):
                raise RuntimeError('Verification baseline is not a successful repair with this manifest')
            baseline_assets = {entry['package']: entry['after'] for entry in baseline['assets']}
            if set(baseline_assets) != ALLOWLIST:
                raise RuntimeError('Verification baseline does not cover exactly the allowlist')
            report['baseline_report'] = str(baseline_file)
            report['baseline_report_sha256'] = sha(baseline_file)
            for package in packages:
                if before[package] != baseline_assets[package]:
                    raise RuntimeError('Fresh-process asset differs from saved repair evidence: ' + package)
                check_only_nanite_changed(before[package], before[package])
            if protected_before != baseline['protected_sha256_after']:
                raise RuntimeError('A protected mesh or engine parent differs from the repair baseline')
        elif mode != 'repair':
            raise ValueError('Mode must be repair or verify')

        pending = [package for package in packages
                   if not before[package]['nanite_enabled'] or not before[package]['nanite_override']]
        report['pending_packages'] = pending
        report['phase'] = 'preflight_passed'
        write_report()
        if mode == 'repair':
            # Complete every backup and re-check every source before the first mutation.
            for entry in report['assets']:
                if entry['package'] in pending:
                    entry['backup'] = backup_asset(root, out, entry['package'], entry['before']['sha256'])
            for package in packages:
                if sha(root / asset_relative_path(package)) != before[package]['sha256']:
                    raise RuntimeError('Source changed during preflight/backup: ' + package)
            report['phase'] = 'repair'
            write_report()
            for entry in report['assets']:
                package = entry['package']
                if package not in pending:
                    entry['action'] = 'already_enabled'
                    continue
                material = assets[package]
                report['active_package'] = package
                entry['action'] = 'mutation_started'
                write_report()
                # Native implementation updates only this instance's usage bitmask.
                edit.set_material_usage_override(material, usage, True, True)
                check_only_nanite_changed(before[package], inspect(package))
                if not lib.save_loaded_asset(material, only_if_is_dirty=False):
                    raise RuntimeError('Failed to save allowlisted MIC: ' + package)
                report['saved_packages'].append(package)
                entry['action'] = 'saved_nanite_override'
                entry['after'] = inspect(package)
                report['active_package'] = None
                write_report()
        for entry in report['assets']:
            package = entry['package']
            after = inspect(package)
            check_only_nanite_changed(before[package], after)
            if (mode == 'verify' or package not in pending) and after['sha256'] != before[package]['sha256']:
                raise RuntimeError('A read-only asset was written: ' + package)
            entry['after'] = after
            if mode == 'verify':
                entry['action'] = 'verified_without_save'
        report['protected_sha256_after'] = {name: sha(file) for name, file in protected.items()}
        if report['protected_sha256_after'] != protected_before:
            raise RuntimeError('A shared parent or source mesh file changed')
        report.update(result='passed', phase='complete', asset_count=len(packages),
                      saved_count=len(report['saved_packages']), verification_requires_fresh_process=(mode == 'repair'))
    except Exception as error:
        report.update(result='failed', error=f'{type(error).__name__}: {error}', traceback=traceback.format_exc())
        raise
    finally:
        write_report()
    print('EXTRACTION_NANITE_USAGE', mode, 'passed', 'saved', report['saved_count'], flush=True)


def main():
    import unreal
    root = Path(unreal.Paths.project_dir()).resolve()
    out = Path(os.environ['EXTRACTION_NANITE_RUN_DIR']).resolve()
    run_root = (root / 'Saved/Extraction/NaniteUsage').resolve()
    if not out.is_relative_to(run_root) or out == run_root or not out.is_dir():
        raise ValueError('Run directory must be newly created by run_nanite_usage.py')
    # The wrapper creates the directory and logs; this exclusive claim rejects
    # replaying the asset task in it, even when a previous run was interrupted.
    if (out / 'Report.json').exists():
        raise FileExistsError('Never overwrite an existing Nanite report')
    with (out / 'AssetTask.started').open('x', encoding='utf-8') as claim:
        claim.write(str(os.getpid()) + '\n')
    mode = os.environ['EXTRACTION_NANITE_MODE']
    baseline = Path(os.environ['EXTRACTION_NANITE_BASELINE']).resolve() if mode == 'verify' else None
    execute(unreal, root, Path(os.environ['EXTRACTION_NANITE_ENGINE_ROOT']).resolve(), out, mode, baseline)


if __name__ == '__main__':
    main()
