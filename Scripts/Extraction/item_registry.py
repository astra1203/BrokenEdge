"""Create a registry once, or validate/export its authored stable identities.

Creation migrates the complete legacy whitelist, including the generated inl.
An existing registry is never regenerated from asset names or C++ fallback IDs.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import unreal

ROOT = Path(__file__).resolve().parents[2]
ASSET = '/Game/Extraction/Registry/DA_ItemRegistry'
OUT = Path(os.environ['EXTRACTION_RUN_DIR'])


def legacy_ids():
    source = (ROOT / 'Source/BrokenEdge/Extraction/Inventory/ExtractionItems.cpp').read_text(encoding='utf-8-sig')
    source = source.replace('#include "ExtractionCollectibles.inl"',
                            (ROOT / 'Source/BrokenEdge/Extraction/Inventory/ExtractionCollectibles.inl').read_text(encoding='utf-8-sig'))
    match = re.search(r'LegacyItemIds\(\).*?Names\s*=\s*\{(.*?)\};', source, re.S)
    assert match, 'Legacy migration source changed'
    ids = re.findall(r'TEXT\("([A-Za-z0-9_]+)"\)', match[1])
    assert ids and len(set(ids)) == len(ids), 'Duplicate or empty legacy identities'
    return ids


def main():
    create = os.environ.get('ITEM_REGISTRY_MODE') == 'item-registry-create'
    exists = unreal.EditorAssetLibrary.does_asset_exist(ASSET)
    assert not create or not exists, 'Registry already exists; validate/export it instead of overwriting'
    assert create or exists, 'Registry is missing; run item-registry-create once'
    if create:
        entries = {}
        for name in legacy_ids():
            definition = unreal.load_asset('/Game/Extraction/Items/DA_Item_' + name)
            assert isinstance(definition, unreal.ExtractionItemDefinition), name
            assert str(definition.get_editor_property('item_id')) == name, name + ': ItemId mismatch'
            entries[name] = definition
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.ExtractionItemRegistry)
        registry = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'DA_ItemRegistry', '/Game/Extraction/Registry', unreal.ExtractionItemRegistry, factory)
        assert registry
        registry.set_editor_property('entries', entries)
    else:
        registry = unreal.load_asset(ASSET)
    errors = list(registry.validate_entries())
    assert not errors, '\n'.join(str(error) for error in errors)
    if create:
        assert unreal.EditorAssetLibrary.save_loaded_asset(registry), ASSET
    entries = {}
    for name, definition in registry.get_editor_property('entries').items():
        # UE Python converts a resolved soft reference to its UObject.
        assert definition, str(name)
        entries[str(name)] = definition.get_path_name()
    source = ROOT / 'Content/Extraction/Registry/DA_ItemRegistry.uasset'
    report = dict(schema_version=1, registry=ASSET + '.DA_ItemRegistry',
                  registry_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  entries=dict(sorted(entries.items())), editor_load='passed')
    text = json.dumps(report, ensure_ascii=False, indent=2) + '\n'
    (OUT / 'ItemRegistry.json').write_text(text, encoding='utf-8')
    (ROOT / 'Config/ExtractionItemRegistry.json').write_text(text, encoding='utf-8')
    print('ITEM_REGISTRY_COMPLETE', len(entries))


if __name__ == '__main__':
    main()
