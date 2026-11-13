"""Inventory saved Enhanced Input assets without modifying them."""
import json
import os
from pathlib import Path
import unreal


def objects(values):
    result = []
    for value in values:
        if not value:
            continue
        properties = {}
        for name in dir(value):
            if name.startswith('_') or name == 'last_value':
                continue
            try:
                field = value.get_editor_property(name)
            except Exception:
                continue
            properties[name] = (field if isinstance(field, (str, int, float, bool)) or field is None
                                else field.get_path_name() if isinstance(field, unreal.Object)
                                else str(field))
        result.append({'class': value.get_class().get_path_name(), 'properties': properties})
    return result


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
report = {'actions': [], 'contexts': [], 'scope': 'Saved project assets; native bindings documented separately.'}
for data in sorted(registry.get_assets_by_path('/Game', recursive=True), key=lambda item: str(item.package_name)):
    kind = str(data.asset_class_path.asset_name)
    if kind not in ('InputAction', 'InputMappingContext'):
        continue
    asset = data.get_asset()
    assert asset, str(data.package_name)
    row = {'asset': asset.get_path_name()}
    if kind == 'InputAction':
        row.update(value_type=str(asset.get_editor_property('value_type')),
                   triggers=objects(asset.get_editor_property('triggers')),
                   modifiers=objects(asset.get_editor_property('modifiers')))
        report['actions'].append(row)
    else:
        row['mappings'] = []
        for mapping in asset.get_editor_property('default_key_mappings').get_editor_property('mappings'):
            action = mapping.get_editor_property('action')
            row['mappings'].append({'action': action.get_path_name() if action else None,
                                    'key': mapping.get_editor_property('key').export_text(),
                                    'triggers': objects(mapping.get_editor_property('triggers')),
                                    'modifiers': objects(mapping.get_editor_property('modifiers'))})
        assert row['mappings'], 'Empty default mapping context: ' + row['asset']
        row['profile_overrides'] = str(asset.get_editor_property('mapping_profile_overrides'))
        report['contexts'].append(row)
assert report['actions'] and report['contexts'], 'No Enhanced Input assets discovered'
report['result'] = 'passed'
out = Path(os.environ['EXTRACTION_RUN_DIR'])
(out / 'InputActions.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('INPUT_ACTION_AUDIT_COMPLETE')
