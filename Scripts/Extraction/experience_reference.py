"""Create/check the approved all-at-once first-phase reference gate.

File hashes and explicit reviews are necessary evidence, not automated proof that
a screenshot/video depicts the claimed client behavior. Review media before
marking it reviewed. A missing client build or a single missing observation blocks B.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

UI = {
    'hud': ['default', 'interaction', 'medical', 'extraction'],
    'loadout': ['default', 'equip', 'unequip', 'invalid'],
    'stash': ['default', 'drag', 'rotate', 'split', 'full'],
    'inventory': ['default', 'drag', 'rotate', 'split', 'close_input'],
    'search': ['unsearched', 'searching', 'revealed', 'taken', 'cancel', 'out_of_range'],
    'details': ['weapon', 'ammo', 'armor', 'attachment', 'medical', 'valuable'],
    'map': ['default', 'self', 'teammate', 'extraction'],
    'spectator': ['downed', 'dead', 'switch_target', 'no_target'],
    'settlement': ['success', 'failure', 'loot_value', 'return_stash'],
}
GUN_OBSERVATIONS = ['default_assembly', 'supported_parts', 'stats_ammo',
                    'hip_single', 'hip_auto', 'ads_single', 'ads_auto',
                    'recoil_recovery', 'muzzle_ejection', 'mechanical_action_audio',
                    'tactical_reload', 'empty_reload', 'swap', 'sprint_to_ready', 'third_person']
REQUIREMENTS = {**{'ui.' + k: v for k, v in UI.items()},
                **{'weapon.' + k: GUN_OBSERVATIONS for k in ('AKM', 'M4A1', 'MP5')},
                **{'container.' + k: ['appearance', 'search_sequence', 'audio', 'item_reveal']
                   for k in ('supply', 'medical', 'equipment', 'valuable')}}


def template():
    return {'schema': 1, 'baseline': {'region': 'CN', 'platform': 'PC', 'client_build': None,
                                     'season': None, 'sampled_at': None},
            'capture_settings': {'resolution': None, 'fov': None, 'input_settings': None},
            'policy': 'All first-phase references must be reviewed before B content production.',
            'groups': {k: {'required_observations': v, 'evidence': []} for k, v in REQUIREMENTS.items()}}


def digest(path):
    sha = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            sha.update(chunk)
    return sha.hexdigest()


def verify(document, base):
    errors, missing, verified = [], {}, []
    baseline = document.get('baseline', {})
    if document.get('schema') != 1:
        errors.append('Unsupported reference schema')
    if baseline.get('region') != 'CN' or baseline.get('platform') != 'PC':
        errors.append('Reference must be CN PC')
    for name in ('client_build', 'season', 'sampled_at'):
        if not baseline.get(name):
            errors.append('Missing baseline.' + name)
    for name in ('resolution', 'fov', 'input_settings'):
        if not document.get('capture_settings', {}).get(name):
            errors.append('Missing capture_settings.' + name)
    groups = document.get('groups', {})
    for unknown in groups.keys() - REQUIREMENTS.keys():
        errors.append('Unknown group ' + unknown)
    checked_files = {}
    for name, observations in REQUIREMENTS.items():
        covered = set()
        has_video = False
        for evidence in groups.get(name, {}).get('evidence', []):
            label = name + ': ' + str(evidence.get('path', '<no path>'))
            if evidence.get('reviewed') is not True or not evidence.get('review_note'):
                errors.append(label + ' requires an actual media review')
                continue
            if not baseline.get('client_build') or evidence.get('client_build') != baseline['client_build']:
                errors.append(label + ' client build mismatch')
                continue
            if evidence.get('kind') not in ('screenshot', 'video', 'audio'):
                errors.append(label + ' unsupported media kind')
                continue
            file = (base / evidence.get('path', '')).resolve()
            if not file.is_file() or file.stat().st_size == 0:
                errors.append(label + ' missing/empty file')
                continue
            if file not in checked_files:
                checked_files[file] = digest(file)
            actual = checked_files[file]
            if evidence.get('sha256') != actual:
                errors.append(label + ' SHA-256 mismatch')
                continue
            claims = set(evidence.get('covers', []))
            if claims - set(observations):
                errors.append(label + ' unknown observation')
                continue
            covered.update(claims)
            has_video |= evidence['kind'] == 'video' and bool(claims)
            verified.append({'group': name, 'path': str(file), 'sha256': actual, 'covers': sorted(claims)})
        if set(observations) - covered:
            missing[name] = sorted(set(observations) - covered)
        if not has_video:
            errors.append(name + ' requires reviewed continuous video')
    return {'result': 'passed' if not errors and not missing else 'blocked',
            'content_production_allowed': not errors and not missing,
            'errors': errors, 'missing_observations': missing, 'verified_evidence': verified,
            'boundary': 'Checks evidence identity/completeness; the recorded media review is still required.'}


def main():
    parser = argparse.ArgumentParser()
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--init', type=Path)
    modes.add_argument('--check', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.init:
        args.init.parent.mkdir(parents=True, exist_ok=True)
        with args.init.open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(template(), ensure_ascii=False, indent=2) + '\n')
        print('REFERENCE_CHECKLIST_CREATED', args.init)
        return 0
    document = json.loads(args.check.read_text(encoding='utf-8-sig'))
    result = verify(document, args.check.resolve().parent)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        with args.report.open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'result': result['result'], 'missing_groups': len(result['missing_observations']),
                      'missing_observations': sum(map(len, result['missing_observations'].values())),
                      'error_count': len(result['errors'])}))
    return 0 if result['content_production_allowed'] else 2


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    raise SystemExit(main())
