"""Remove only unreferenced assets from the interrupted, owned Phase7E import."""
from pathlib import Path
import subprocess
import zipfile
import unreal


def cleanup(g):
    root, work, out, lib = g['ROOT'], g['WORK'], g['OUT'], g['LIB']
    recovery = work / 'ImportBaselineRecovery'
    # The first attempt used the initial Phase7EExpanded workspace.
    if not recovery.exists():
        g['report']['cleanup'] = {'removed': [], 'reason': 'No interrupted import ledger'}
        return
    previous = g['read'](recovery / 'LedgerBefore.json')
    changed = set(g['read'](recovery / 'ModifiedAssets.json'))
    candidates = set()
    for record in previous.values():
        asset = record.get('asset', '').split('.')[0]
        file = 'Content/' + asset.removeprefix('/Game/') + '.uasset'
        if file in changed and '/Restored/Assets/' in asset:
            candidates.add(asset.split('/Assets/')[0] + '/Assets/' + asset.split('/Assets/')[1].split('/')[0])
    used = [v.get('asset', '').split('.')[0] for v in g['state'].values()]
    candidates = {folder for folder in candidates if not any(p.startswith(folder + '/') for p in used)}
    tracked = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', 'HEAD', '--',
                                       'Content/Maps/SpaceCenter/Restored'], cwd=root, text=True).splitlines()
    for folder in candidates:
        prefix = 'Content/' + folder.removeprefix('/Game/') + '/'
        assert not any(p.startswith(prefix) for p in tracked), 'Cleanup candidate exists in HEAD'
    unreal.AssetRegistryHelpers.get_asset_registry().search_all_assets(True)
    blocked = {}
    # Remove a folder only when none of its assets has a referencer outside the removal set.
    while True:
        unsafe = {}
        for folder in candidates:
            for asset in lib.list_assets(folder, recursive=True):
                refs = lib.find_package_referencers_for_asset(asset, True)
                external = [str(r) for r in refs if not any(str(r).startswith(c + '/') for c in candidates)]
                if external:unsafe[folder] = external
        if not unsafe:break
        blocked.update(unsafe)
        candidates.difference_update(unsafe)
    archived = []
    with zipfile.ZipFile(out / 'AbandonedAssets.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for folder in sorted(candidates):
            disk = (root / 'Content' / folder.removeprefix('/Game/')).resolve()
            assert disk.is_relative_to((root / 'Content/Maps/SpaceCenter/Restored/Assets').resolve())
            for file in disk.rglob('*'):
                if file.is_file():
                    archive.write(file, file.relative_to(root).as_posix())
                    archived.append({'file':file.relative_to(root).as_posix(),'sha256':g['sha'](file)})
    g['write_json'](out / 'BackupIndex.json', archived)
    removed = []
    for folder in sorted(candidates):
        assert lib.delete_directory(folder), folder
        removed.append(folder)
    g['report']['cleanup'] = {'removed':removed,'retained_referenced':blocked,
                              'backup':str(out / 'AbandonedAssets.zip')}
