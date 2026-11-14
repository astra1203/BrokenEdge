"""Load and compile project-module Blueprint referencers without saving assets."""
import json
import os
from pathlib import Path
import unreal

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
packages = set(map(str, registry.get_referencers('/Script/BrokenEdge', unreal.AssetRegistryDependencyOptions())))
results, failures = [], []
for asset in registry.get_all_assets():
    tags = {key: str(asset.get_tag_value(key) or '') for key in ('ParentClass', 'NativeParentClass')}
    if (str(asset.package_name) not in packages and not any('/Script/BrokenEdge.' in value for value in tags.values())) or 'Blueprint' not in str(asset.asset_class_path.asset_name):
        continue
    entry = dict(package=str(asset.package_name), asset_type=str(asset.asset_class_path.asset_name))
    blueprint = asset.get_asset()
    entry['loaded'] = blueprint is not None
    if blueprint:
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        entry['status'] = str(blueprint.get_editor_property('status'))
        entry['parents'] = tags
        entry['parents_loaded'] = {}
        for key, tagged in entry['parents'].items():
            path = tagged.split("'", 1)[-1].rstrip("'")
            if path:
                entry['parents_loaded'][key] = unreal.load_class(None, path) is not None
    entry['passed'] = bool(blueprint) and 'BS_ERROR' not in entry.get('status', '').upper() and all(entry.get('parents_loaded', {}).values())
    results.append(entry)
    if not entry['passed']: failures.append(entry['package'])
results.sort(key=lambda entry: entry['package'])
out = Path(os.environ['EXTRACTION_RUN_DIR'])
(out / 'NativeBlueprints.json').write_text(json.dumps(dict(blueprints=results, failed=failures), ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
assert results and not failures, failures
print('NATIVE_BLUEPRINT_AUDIT_COMPLETE', len(results))
