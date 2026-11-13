"""Read-only native prototype referencer/Blueprint-parent audit. Never saves assets."""
import json
import os
from pathlib import Path
import unreal

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
names = ('MyCharacter', 'CombatObserver')
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
# Package referencers cannot resolve individual native classes. Keep the types in place
# until graph/property-level references have also been checked; do not infer safe moves.
report['scope'] = 'Package referencers and Blueprint parent tags; graph/property references not certified.'
out = Path(os.environ['EXTRACTION_RUN_DIR'])
(out / 'PrototypeReferences.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('PROTOTYPE_REFERENCE_AUDIT_COMPLETE')
