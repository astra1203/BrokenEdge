"""Import source candidates into an isolated folder; do not equip incomplete guns."""
import hashlib
import json
from pathlib import Path
import unreal


def run():
    root = Path(unreal.Paths.project_dir())
    source = root/'Saved/Extraction/SpaceCenterExpansionSources01'
    manifest = json.loads((source/'Manifest.json').read_text(encoding='utf-8'))
    destination = '/Game/Extraction/Weapons/SourceCandidates'
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    results = []
    for name, relative in manifest['models'].items():
        # Asset names cannot contain the source's hyphenated caliber designators.
        folder = destination+'/'+name.replace('-', '_')
        existing = list(unreal.EditorAssetLibrary.list_assets(folder, recursive=True))
        if existing:
            results.append({'source': relative, 'assets': existing, 'status': 'existing_not_reimported'})
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', str(source/relative))
        task.set_editor_property('destination_path', folder)
        task.set_editor_property('automated', True)
        task.set_editor_property('save', True)
        task.set_editor_property('replace_existing', False)
        tools.import_asset_tasks([task])
        assets = list(unreal.EditorAssetLibrary.list_assets(folder, recursive=True))
        assert assets, relative
        results.append({'source': relative, 'sha256': hashlib.sha256((source/relative).read_bytes()).hexdigest(),
                        'assets': assets, 'status': 'imported_not_assembled'})
    out = root/'Saved/Extraction/SpaceCenterExpansion01/ImportedCandidates.json'
    out.write_text(json.dumps({'models': results, 'visual_validation': 'not_run', 'playable_weapon': False},
                              ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('EXPANSION_CANDIDATES_IMPORTED',len(results))


if __name__ == '__main__':
    run()
