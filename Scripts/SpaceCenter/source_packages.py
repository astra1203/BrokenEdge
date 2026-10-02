"""Read-only ZIP catalog and bounded extraction of DeltaForce cooked packages.

Source archives are never modified. The custom summary reader is only a dependency
preflight; CUE4Parse is responsible for decoding exports and mesh/texture payloads.
"""
import argparse
from collections import deque
import hashlib
import json
from pathlib import Path, PurePosixPath
import sqlite3
import struct
import zipfile

PROJECT = Path(__file__).resolve().parents[2]
WORK = PROJECT / 'Saved/SpaceCenter'
SOURCE = Path('D:/司晨/源代码和素材/三角洲解包资产/原始资源ZIP')
PAYLOADS = {'.uasset', '.umap', '.uexp', '.ubulk', '.uptnl'}


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def package_key(path):
    path = path.replace('\\', '/').strip()
    if path.startswith('/Game/'):
        path = 'DeltaForce/Content/' + path[6:]
    if path.startswith('/Engine/'):
        path = 'Engine/Content/' + path[8:]
    if PurePosixPath(path).suffix.lower() in PAYLOADS:
        path = path.rsplit('.', 1)[0]
    return path.casefold()


def index_archives(source=SOURCE):
    WORK.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(WORK / 'catalog.sqlite')
    db.execute('CREATE TABLE IF NOT EXISTS entries(package TEXT, ext TEXT, archive TEXT, path TEXT, size INTEGER, crc INTEGER)')
    db.execute('DELETE FROM entries')
    archives = []
    for archive in sorted(source.glob('*.zip')):
        with zipfile.ZipFile(archive) as z:
            records = [(package_key(i.filename), PurePosixPath(i.filename).suffix.lower(),
                        str(archive), i.filename, i.file_size, i.CRC)
                       for i in z.infolist() if PurePosixPath(i.filename).suffix.lower() in PAYLOADS]
            db.executemany('INSERT INTO entries VALUES(?,?,?,?,?,?)', records)
            archives.append({'archive': archive.name, 'size': archive.stat().st_size,
                             'mtime_ns': archive.stat().st_mtime_ns, 'entries': len(records)})
    db.execute('CREATE INDEX IF NOT EXISTS package_index ON entries(package)')
    db.commit()
    count = db.execute('SELECT COUNT(*) FROM entries').fetchone()[0]
    duplicates = db.execute('SELECT path, COUNT(*) FROM entries GROUP BY package,ext HAVING COUNT(*)>1').fetchall()
    summary = {'archives': archives, 'entry_count': count, 'duplicates': duplicates}
    write_json(WORK / 'ArchiveIndex.json', summary)
    print(json.dumps({'archives': len(archives), 'entries': count, 'duplicates': len(duplicates)}))
    db.close()


def read_names(data):
    """Bounded reader for this version's tagged-property package name table."""
    if len(data) < 32 or struct.unpack_from('<I', data)[0] != 0x9E2A83C1:
        raise ValueError('Unsupported package signature')
    legacy, _, _, _, customs = struct.unpack_from('<5i', data, 4)
    if legacy != -4613 or not 0 <= customs <= 1000:
        raise ValueError('Unsupported DeltaForce summary version')
    pos = 24 + 20 * customs
    _, length = struct.unpack_from('<ii', data, pos)
    pos += 8 + abs(length) * (2 if length < 0 else 1)
    _, count, pos = struct.unpack_from('<Iii', data, pos)
    if not 0 <= count <= 1000000 or not 0 <= pos < len(data):
        raise ValueError('Invalid name table')
    names = []
    for _ in range(count):
        length = struct.unpack_from('<i', data, pos)[0]
        pos += 4
        size = abs(length) * (2 if length < 0 else 1)
        if not 0 < size <= len(data) - pos:
            raise ValueError('Invalid name length')
        names.append(data[pos:pos + size].decode('utf-16-le' if length < 0 else 'utf-8').rstrip('\0'))
        pos += size + 4
    return names


def extract(packages, recursive=False, limit_gib=15):
    db = sqlite3.connect(WORK / 'catalog.sqlite')
    db.row_factory = sqlite3.Row
    out = WORK / 'Source'
    pending, seen, missing, entries = deque(packages), set(), [], []
    total, limit = 0, limit_gib * 1024**3
    handles = {}
    try:
        while pending:
            package = pending.popleft()
            key = package_key(package)
            if key in seen:
                continue
            seen.add(key)
            rows = db.execute('SELECT * FROM entries WHERE package=? ORDER BY ext,archive', (key,)).fetchall()
            if not rows:
                missing.append(package)
                continue
            suffixes = set()
            for row in rows:
                if row['ext'] in suffixes:
                    raise ValueError('Ambiguous duplicate package: ' + row['path'])
                suffixes.add(row['ext'])
                total += row['size']
                if total > limit:
                    raise ValueError('Extraction budget exceeded; narrow the selection')
                relative = PurePosixPath(row['path'])
                dest = out.joinpath(*relative.parts).resolve()
                if not dest.is_relative_to(out.resolve()):
                    raise ValueError('Archive path escapes extraction root')
                if row['archive'] not in handles:
                    handles[row['archive']] = zipfile.ZipFile(row['archive'])
                data = handles[row['archive']].read(row['path'])  # ZIP CRC checked by zipfile
                digest = hashlib.sha256(data).hexdigest()
                if not dest.exists() or hashlib.sha256(dest.read_bytes()).hexdigest() != digest:
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(data)
                entries.append({'archive': Path(row['archive']).name, 'path': row['path'],
                                'size': len(data), 'sha256': digest, 'crc': row['crc']})
                if recursive and row['ext'] in {'.uasset', '.umap'}:
                    pending.extend(n for n in read_names(data) if n.startswith('/Game/') and '.' not in n)
        report = {'requested': packages, 'recursive': recursive, 'bytes': total,
                  'packages': len(seen), 'entries': entries, 'missing': sorted(missing)}
        tag = hashlib.sha256(('\n'.join(packages) + str(recursive)).encode()).hexdigest()[:12]
        write_json(WORK / 'Extractions' / (tag + '.json'), report)
        print(json.dumps({'report': str(WORK / 'Extractions' / (tag + '.json')),
                          'bytes': total, 'packages': len(seen), 'missing': len(missing)}))
        return report
    finally:
        db.close()
        for handle in handles.values():
            handle.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['index', 'extract'])
    parser.add_argument('packages', nargs='*')
    parser.add_argument('--recursive', action='store_true')
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--limit-gib', type=float, default=15)
    args = parser.parse_args()
    if args.command == 'index':
        index_archives(args.source)
    else:
        if not args.packages:
            parser.error('extract requires at least one complete package path')
        extract(args.packages, args.recursive, args.limit_gib)
