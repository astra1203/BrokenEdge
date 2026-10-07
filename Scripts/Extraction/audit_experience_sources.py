"""Read-only ZIP inventory and bounded GLB dependency checks for Experience A.

Converted files are candidates, not proof of a complete weapon, correct version,
working animation, or reviewed presentation. No source ZIP is extracted/changed.
"""
import argparse
import datetime
import hashlib
import json
import posixpath
from pathlib import Path
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('D:/司晨/源代码和素材/三角洲解包资产/网盘上传压缩包')
WEAPONS = {'AKM': ('akm',), 'M4A1': ('m4a1',), 'MP5': ('mp5',),
           'K416': ('hk416', 'k416'), 'SR-25': ('sr25', 'sr-25'),
           'AWM': ('awm',), 'M870': ('m870',), 'M249': ('m249',)}
RECEIVERS = {'M4A1': 'Rec_AR_M4A1-Std_001', 'MP5': 'Rec_SMG_MP5A4-Std_004'}


def inspect_glb(archive, entry, index):
    with zipfile.ZipFile(archive) as z:
        info = z.getinfo(entry)
        if info.file_size > 64 * 1024 * 1024:
            raise ValueError('GLB exceeds the bounded audit size: ' + entry)
        data = z.read(entry)  # ZipFile also verifies the entry CRC.
    if len(data) < 20:
        raise ValueError('Truncated GLB: ' + entry)
    magic, version, length, size, kind = struct.unpack_from('<5I', data)
    if magic != 0x46546C67 or version != 2 or length != len(data) or kind != 0x4E4F534A or 20 + size > length:
        raise ValueError('Invalid GLB header: ' + entry)
    document = json.loads(data[20:20 + size])
    if not document.get('meshes'):
        raise ValueError('GLB contains no meshes: ' + entry)
    dependencies = []
    for image in document.get('images', []):
        uri = image.get('uri')
        if not uri or uri.startswith('data:'):
            continue
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(entry), uri))
        if resolved.startswith('../') or resolved.startswith('/') or '://' in resolved:
            raise ValueError('Unexpected external image URI: ' + entry)
        matches = index.get(resolved, [])
        dependencies.append({'entry': resolved, 'archives': [p.name for p in matches],
                             'status': 'indexed' if matches else 'missing'})
    return {'archive': archive.name, 'entry': entry, 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest(), 'meshes': len(document['meshes']),
            'skins': len(document.get('skins', [])), 'nodes': len(document.get('nodes', [])),
            'image_dependencies': dependencies, 'structural_check': 'passed',
            'ue_import': 'not_run', 'visual_review': 'not_run', 'complete_weapon': False}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
        parser.error('Use a new alphanumeric run tag')
    source = args.source.resolve()
    archives = sorted({p for pattern in ('*Models_*.zip', '*GenTexDir*.zip', '*UI_*.zip',
                                         '*UI_HD_*.zip', '*DataTables_*.zip', '视频_*.zip')
                       for p in source.glob(pattern)})
    if not archives:
        parser.error('No source archives found')
    out = ROOT / 'Saved/Extraction' / args.tag
    out.mkdir(parents=True, exist_ok=False)
    index, inventories, model_entries, videos, tables = {}, [], [], [], []
    for archive in archives:
        before = archive.stat()
        with zipfile.ZipFile(archive) as z:
            entries = [i for i in z.infolist() if not i.is_dir()]
            for info in entries:
                index.setdefault(info.filename, []).append(archive)
                if '/Models/Weapons/' in info.filename:
                    model_entries.append((archive, info.filename))
                if info.filename.lower().endswith(('.bk2', '.mp4', '.webm', '.mov')):
                    videos.append({'archive': archive.name, 'entry': info.filename})
                if 'DataTables' in archive.name and any(k in info.filename.lower() for k in ('weapon', 'gun', 'part', 'ammo')):
                    tables.append({'archive': archive.name, 'entry': info.filename})
        after = archive.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise RuntimeError('Source changed during audit: ' + str(archive))
        inventories.append({'archive': archive.name, 'bytes': before.st_size,
                            'mtime_ns': before.st_mtime_ns, 'entries': len(entries)})
    weapons, checked = {}, []
    for name, aliases in WEAPONS.items():
        hits = [(p, n) for p, n in model_entries if any(alias in n.lower() for alias in aliases)]
        receivers = [{'archive': p.name, 'entry': n} for p, n in hits
                     if '/Parts/Rec/' in n and n.endswith('.glb')]
        animations = [{'archive': p.name, 'entry': n} for p, n in hits if n.endswith('.psa')]
        weapons[name] = {'name_matched_entries': len(hits), 'receiver_candidates': receivers,
                         'animation_candidates': animations, 'status': 'unverified_candidates',
                         'note': 'Name matches do not establish weapon definitions, shared-part mapping, or completeness.'}
        if name in RECEIVERS:
            receiver = RECEIVERS[name]
            for view in ('1P', '3P', 'UI'):
                matches = [(p, n) for p, n in hits if n.endswith('/' + receiver + '/' + receiver + '_' + view + '.glb')]
                for p, n in matches:
                    checked.append({'weapon': name, 'view': view, **inspect_glb(p, n, index)})
    report = {'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_root': str(source), 'result': 'inventory_complete_reference_gate_not_passed',
              'reference_build': None, 'source_archives': inventories, 'weapons': weapons,
              'checked_receiver_glbs': checked, 'candidate_definition_tables': tables,
              'video_inventory': videos,
              'boundaries': ['ZIP names and GLB structure are inspected; no UE import/playback acceptance.',
                             'Built-in Bink movies are not same-version gameplay capture evidence.',
                             'Missing name matches require shared receiver mapping from definitions before calling assets missing.',
                             'Reference client build, full assembly, animation retargeting, audio mapping and visual quality remain unverified.']}
    (out / 'Report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'result': report['result'], 'archives': len(archives), 'checked_glbs': len(checked),
                      'missing_image_dependencies': sum(d['status'] == 'missing' for g in checked for d in g['image_dependencies']),
                      'report': str(out / 'Report.json')}, ensure_ascii=False))


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
