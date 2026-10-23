"""Inventory the approved SCAR-H/knife/map/UI expansion without modifying sources.

ZIP central directories locate candidates; hashes identify selected reference tables.
Neither a name match nor a parsed table constitutes runtime or visual acceptance.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import zipfile

GROUPS = {
    'scar': re.compile(r'(scar[-_]?h|scarh|scar[_-](?:long|std)|scar-h)', re.I),
    'knife': re.compile(r'(Melees/Knife/|WeaponAnim.*Melee/|Melee_M_Knife)', re.I),
    'map': re.compile(r'(SpaceCity|SpaceCenter|Space_City|Space_Center)', re.I),
    'rules': re.compile(r'(Extract|Evac|Escape|Spawn|BornPoint|Deployment)', re.I),
    'ui': re.compile(r'(Inventory|Backpack|Stash|Loadout|Settlement|HUD|Minimap)', re.I),
}


def inventory(source):
    groups = {key: [] for key in GROUPS}
    archives = []
    tables = []
    converted = source / '网盘上传压缩包'
    selected = [p for p in sorted(converted.glob('*.zip')) if any(
        word in p.name for word in ('Models_', 'Maps_', 'DataTables_', 'UI_', 'UI_HD_'))]
    selected += sorted((source / '原始资源ZIP').glob('*Lua*.zip'))
    for path in selected:
        before = path.stat()
        with zipfile.ZipFile(path) as archive:
            entries = [info for info in archive.infolist() if not info.is_dir()]
            for info in entries:
                name = info.filename.replace('\\', '/')
                matched = [key for key, pattern in GROUPS.items() if pattern.search(name)]
                for key in matched:
                    groups[key].append({'archive': str(path.relative_to(source)), 'entry': name,
                                        'bytes': info.file_size, 'crc32': f'{info.CRC:08x}'})
                if matched and name.endswith('.json') and info.file_size <= 8 * 1024 * 1024:
                    if any(key in matched for key in ('scar', 'map', 'rules')):
                        data = archive.read(info)
                        try:
                            document = json.loads(data.decode('utf-8-sig'))
                            rows = document.get('Rows', {}) if isinstance(document, dict) else {}
                            tables.append({'archive': str(path.relative_to(source)), 'entry': name,
                                           'sha256': hashlib.sha256(data).hexdigest(),
                                           'rows': list(rows) if isinstance(rows, dict) else [],
                                           'status': 'parsed_unreviewed'})
                        except (ValueError, UnicodeError):
                            tables.append({'entry': name, 'status': 'parse_failed'})
        after = path.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise RuntimeError(f'Source changed during audit: {path}')
        archives.append({'path': str(path.relative_to(source)), 'bytes': before.st_size,
                         'mtime_ns': before.st_mtime_ns, 'entries': len(entries)})
    return {'schema': 1, 'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'source': str(source), 'archives': archives, 'candidates': groups, 'tables': tables,
            'reference_build': None, 'status': 'inventory_only',
            'limitations': ['Source build, default assemblies and map variant not yet established.',
                            'No official coordinates, timings or numeric weapon balance inferred from names.',
                            'No UE import, animation playback or PIE acceptance claimed.']}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Choose a new output path to preserve prior evidence')
    report = inventory(args.source.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': report['status'], 'archives': len(report['archives']),
                      'groups': {k: len(v) for k, v in report['candidates'].items()},
                      'tables': len(report['tables'])}))


if __name__ == '__main__':
    main()
