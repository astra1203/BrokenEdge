"""Recover a backed-up orphaned prepared raid through UE, first on a copy, then idempotently on the original."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]


def read_store(path):
    data = (path / 'Store.journal').read_bytes()
    offset, sequence, state = 0, 0, None
    while offset < len(data):
        assert len(data) - offset >= 16, 'Partial journal header'
        magic, version, size, crc = struct.unpack_from('<IIII', data, offset)
        offset += 16
        body = data[offset:offset + size]
        assert magic == 0x34455842 and version == 1 and len(body) == size and zlib.crc32(body) == crc
        state = json.loads(body.decode('utf-8'))
        sequence += 1
        assert state['sequence'] == sequence
        offset += size
    assert state is not None
    return state, hashlib.sha256(data).hexdigest()


def inventory(profile):
    view = profile['view']
    return view['stash']['items'] + view['loadout']['items'] + view['overflow']


def totals(items):
    counts = Counter()
    for item in items:
        counts[item['definition']] += item['quantity']
        if item.get('magazine', 0):
            counts[item['definition'] + '.magazine'] += item['magazine']
    return counts


def verify(before, after):
    assert before['id'] == after['id']
    profiles = {p['view']['profile']: p for p in after['profiles']}
    assert set(profiles) == {p['view']['profile'] for p in before['profiles']}
    refunds = {}
    for raid in before['raids']:
        if raid['closed']:
            assert raid == next(r for r in after['raids'] if r['id'] == raid['id'])
            continue
        assert not raid['active'], 'This repair is limited to prepared raids'
        fixed = next(r for r in after['raids'] if r['id'] == raid['id'])
        assert fixed['closed'] and not fixed['active']
        for member in raid['players']:
            assert member['outcome'] == 'None'
            refunds.setdefault(member['profile'], []).extend(member['original']['items'])
        assert all(p['outcome'] == 'Aborted' for p in fixed['players'])
    result = []
    for old in before['profiles']:
        key = old['view']['profile']
        new = profiles[key]
        assert old['credentialHash'] == new['credentialHash'], 'Identity changed'
        expected = inventory(old) + refunds.get(key, [])
        assert totals(expected) == totals(inventory(new)), 'Item or magazine quantity changed'
        old_rifles = sorted(i['id'] for i in expected if i['definition'] == 'AK')
        new_rifles = sorted(i['id'] for i in inventory(new) if i['definition'] == 'AK')
        assert old_rifles == new_rifles, 'Rifle identity changed or duplicated'
        if key in refunds:
            assert new['view']['result']['outcome'] == 'Aborted'
        result.append(dict(totals(inventory(new))))
    return result


def require_no_writer():
    command = "if (Get-Process UnrealEditor,UnrealEditor-Cmd,BrokenEdge,BrokenEdgeServer -ErrorAction SilentlyContinue) { exit 1 }"
    subprocess.run(['powershell.exe', '-NoProfile', '-Command', command], check=True, capture_output=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    parser.add_argument('--backup', type=Path, required=True, help='Existing backup directory containing Store.journal')
    args = parser.parse_args()
    if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
        raise ValueError('Use an alphanumeric run tag')
    original = ROOT / 'Saved/Extraction/Persistence'
    require_no_writer()
    before, original_hash = read_store(original)
    saved, backup_hash = read_store(args.backup)
    assert original_hash == backup_hash and before == saved, 'Backup must exactly match the original'
    pending = [r for r in before['raids'] if not r['closed']]
    assert pending and all(not r['active'] for r in pending), 'Expected only orphaned prepared records'
    out = ROOT / 'Saved/Extraction' / args.tag
    out.mkdir(parents=True, exist_ok=False)
    copy = out / 'CopyStore'
    shutil.copytree(args.backup, copy)
    runs = []

    def recover(path, suffix):
        require_no_writer()
        tag = args.tag + suffix
        subprocess.run([sys.executable, str(ROOT / 'Scripts/Extraction/run_editor_task.py'),
                        'recover-existing', '--tag', tag, '--store', str(path)], cwd=ROOT, check=True)
        runs.append('Saved/Extraction/' + tag)
        return read_store(path)

    repaired_copy, _ = recover(copy, 'Copy')
    verify(before, repaired_copy)
    assert read_store(original)[1] == original_hash, 'Original changed during copy validation'
    repaired, repaired_hash = recover(original, 'Original')
    counts = verify(before, repaired)
    assert repaired == repaired_copy, 'Original and copy recovery disagree'
    repeated, repeated_hash = recover(original, 'Repeat')
    assert repaired == repeated and repaired_hash == repeated_hash, 'Repeated load must not refund twice'
    report = {'result': 'passed', 'backup_sha256': backup_hash, 'original_before_sha256': original_hash,
              'original_after_sha256': repaired_hash, 'pending_records_before': len(pending),
              'pending_records_after': sum(not r['closed'] for r in repaired['raids']),
              'profiles': len(repaired['profiles']), 'quantities_after': counts,
              'identity_and_rifle_ids_preserved': True, 'repeat_load_byte_identical': True, 'runs': runs}
    (out / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
