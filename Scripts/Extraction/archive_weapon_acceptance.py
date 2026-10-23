"""Archive bounded, non-account acceptance metadata; raw run snapshots stay in Saved."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--runs', nargs='+', required=True)
parser.add_argument('--output', type=Path, default=ROOT / 'Docs/Extraction/WeaponMechanicsEvidence/Runtime/Acceptance.json')
args = parser.parse_args()
result = {'scope': 'Project compatibility implementation; not official fidelity', 'runs': []}
for tag in args.runs:
    assert tag and all(c.isalnum() or c in '-_' for c in tag)
    folder = ROOT / 'Saved/Extraction' / tag
    path = folder / ('Report.json' if (folder / 'Report.json').exists() else 'Run.json')
    run = json.loads(path.read_text(encoding='utf-8-sig'))
    entry = {'tag': tag, 'record': path.relative_to(ROOT).as_posix(),
             'record_sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    for key in ['result', 'exit_code', 'error', 'started_utc', 'finished_utc', 'successful_tests', 'scope', 'elapsed_seconds']:
        if key in run:
            entry[key] = run[key]
    if 'result' not in entry:
        entry['result'] = 'passed' if run.get('exit_code') == 0 else 'failed'
    if 'executable' in run:
        entry['executable'] = {k: v for k, v in run['executable'].items() if k != 'path'}
    if 'executables' in run:
        entry['executables'] = {role: {k: v for k, v in executable.items() if k != 'path'}
                                for role, executable in run['executables'].items()}
    if 'checks' in run:
        entry['checks'] = [check['name'] for check in run['checks']]
        entry['options'] = {k: v for k, v in run['options'].items()
                            if k in ['scenario', 'players', 'rendered_clients', 'lag', 'packaged', 'package_root']}
    if run.get('resources'):
        entry['resources'] = {
            'sample_count': len(run['resources']),
            'min_ram_free_gib': min(s['ram_free_gib'] for s in run['resources']),
            'max_commit_used_gib': max(s['commit_used_gib'] for s in run['resources'])}
    result['runs'].append(entry)
result['source_sha256_lf'] = {}
for directory in ['Source/BrokenEdge/Weapon', 'Source/BrokenEdge/Extraction', 'Source/BrokenEdge/Tests']:
    for path in sorted((ROOT / directory).glob('*')):
        if path.suffix in ['.h', '.cpp']:
            result['source_sha256_lf'][path.relative_to(ROOT).as_posix()] = hashlib.sha256(
                path.read_text(encoding='utf-8-sig').encode('utf-8')).hexdigest()
for name in ['Source/BrokenEdge/Component/CombatComponent.cpp', 'Scripts/Extraction/weapon_mechanics_scenarios.py',
             'Scripts/Extraction/run_network.py', 'Scripts/Extraction/run_editor_task.py',
             'Config/DefaultGame.ini']:
    result['source_sha256_lf'][name] = hashlib.sha256((ROOT / name).read_text(encoding='utf-8-sig').encode('utf-8')).hexdigest()
args.output.parent.mkdir(parents=True, exist_ok=True)
result['asset_sha256'] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in [
    'Content/Extraction/Training/L_WeaponRange.umap', 'Content/Extraction/Training/DA_WeaponRangeMap.uasset',
    'Content/Extraction/Data/WeaponMechanics.json', 'Content/Extraction/Equipment/Compatibility/GripRecoil.uasset']}
args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('ACCEPTANCE_ARCHIVED', len(result['runs']), args.output)
