"""Deterministic PC extraction snapshot. Does not rewrite source archives or UE assets."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def import_snapshot(source):
    archives = source / '网盘上传压缩包'
    main = zipfile.ZipFile(next(p for p in archives.glob('*DataTables_001.zip') if 'Redirect' not in p.name))
    pc = zipfile.ZipFile(next(archives.glob('*R13N_001.zip')))
    evidence = {}

    def rows(table, field=None, value=None, pc_only=False, many=False):
        archive = pc if pc_only else main
        names = [n for n in archive.namelist() if n.endswith('/' + table + '.json')
                 and (not pc_only or '/PC/' in n)]
        assert len(names) == 1, (table, names)
        name = names[0]
        raw = archive.read(name)
        data = json.loads(raw)['Rows']
        selected = data if field is None else {k: v for k, v in data.items() if v.get(field) == value}
        assert selected, (table, field, value)
        key = ('PC/' if pc_only else '') + table
        entry = evidence.setdefault(key, dict(archive=Path(archive.filename).name, member=name,
                                             sha256=hashlib.sha256(raw).hexdigest(), rows={}))
        entry['rows'].update(selected)
        if field is None or many:
            return selected
        assert len(selected) == 1, (table, value, list(selected))
        return next(iter(selected.values()))

    profiles = {}
    for item, rec in [('AK', 18010000006), ('SCARH', 18010000021)]:
        base = rows('WeaponAttributeTable', 'RecId', rec)
        aim = rows('WeaponAimingTable', 'AimingId', base['AimingId'])
        bullet = rows('WeaponBulletTable', 'BulletId', base['BulletFlyingId'])
        breath = rows('WeaponHoldBreathTable', 'HoldBreathId', rec)
        recoil = {}
        spread = {}
        for mode, rid, sid in [('hip', base['WaistShootRecoilId'], base['WaistShootSpreadId']),
                               ('ads', aim['RecoilId'], aim['SpreadId'])]:
            recoil[mode] = rows('WeaponRecoilTable', 'RecoilId', rid, True)
            spread[mode] = rows('WeaponSpreadTable', 'SpreadId', sid)
        profiles[item] = dict(recId=rec, base=base, aim=aim, bullet=bullet, breath=breath,
                              recoil=recoil, spread=spread)
        profiles[item]['stance'] = rows('WeaponCharacterStateTable', 'CharacterStateId', base['CharacterStateId'])
        preset_id = 10010000012 if item == 'AK' else 10010000228
        profiles[item]['preset'] = rows('GunPresetTable', 'ItemId', preset_id)
        profiles[item]['assembly'] = rows('GunDescTable', 'DesId', preset_id, many=True)
    # Keep operation definitions/curves for audit, without pretending unknown operations are implemented.
    for table in ['WeaponMainAttributeTable', 'PartSocketsTable', 'PartsRuleGroupTable',
                  'PartsForbidRuleTable', 'TunePartFunctionTable']:
        rows(table)
    curves = {}
    for name in main.namelist():
        if not name.endswith('.json'):
            continue
        if 'Curve' in name and '/MP/' not in name and '_MP' not in name:
            raw = main.read(name)
            data = json.loads(raw)
            if data.get('Type') in ('CurveFloat', 'CurveVector'):
                curves[name] = dict(sha256=hashlib.sha256(raw).hexdigest(), data=data)
    result = dict(schema=1, mode='ExtractionPC', profiles=profiles, evidence=evidence, curves=curves,
                  limitations=['Source build date unknown; not a live-game balance claim',
                               'Base rows are not resolved standard-preset stats',
                               'Recoil angular units and DFM composition unverified',
                               'BlendMode_Dest runtime semantics unverified',
                               'SOL damage branch and part operation precedence unverified'])
    canonical = json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(',', ':'))
    result['revision'] = int(hashlib.sha256(canonical.encode()).hexdigest()[:7], 16)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=Path('D:/司晨/源代码和素材/三角洲解包资产'))
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    output = ROOT / 'Content/Extraction/Data/WeaponMechanics.json'
    payload = json.dumps(import_snapshot(args.source), ensure_ascii=False, sort_keys=True, indent=2) + '\n'
    if args.check:
        assert output.read_text(encoding='utf-8') == payload, 'Weapon snapshot drift'
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(payload, encoding='utf-8', newline='\n')
    print(f'Weapon snapshot verified: {len(payload)} characters')
