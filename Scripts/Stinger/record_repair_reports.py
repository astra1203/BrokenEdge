"""Archive repair evidence separately from the original checkpoint reports."""
from pathlib import Path
from collections import Counter
import json
import shutil

project = Path(__file__).resolve().parents[2]
saved = project / 'Saved' / 'Stinger'
docs = project / 'Artifacts' / 'Stinger'
manifest = json.loads((saved / 'import_manifest_v3.json').read_text(encoding='utf-8'))
entries = list(manifest['entries'].values())
sequences = [s for e in entries for s in e.get('sequences', [])]
assert len(entries) == 443 and all(e.get('mapping_revision') == 4 and e.get('status') == 'written' for e in entries)
poses = json.loads((saved / 'repair_pose_check.json').read_text(encoding='utf-8'))
mapped = [s['assets']['Mapped'] for s in sequences if 'Mapped' in s['assets']]
assert set(mapped) == set(poses), 'Numeric pose report does not cover every mapped asset'
summary = {
    'checkpoint': '9b0525fbfac155aecdd6b976309616202dc78043',
    'branch': 'fix/stinger-animation-import',
    'files_written': len(entries),
    'sequence_validation': dict(Counter(s['validation'] for s in sequences)),
    'mapped_pose_checks': len(poses),
    'source_only_sequences': len(sequences) - len(mapped),
    'visual_acceptance': 'not_completed',
    'remaining': ['ordinary third-person rifle animations are absent',
                  'additive base pose and visibility metadata are absent for 40 clips',
                  'AK first-person sight alignment and full animation visual review remain'],
    'roles': {
        'Idle': 'AR_M_1P_Idle01', 'Aim': 'AR_M_1P_AimIdle_Pose',
        'Sprint': 'AR_M_1P_RelaxIdle', 'Fire': 'AR_M_1P_Fire',
        'Reload': 'AR_M_1P_ChangeClipFull', 'Equip': 'AR_M_1P_GetWeapon',
        'Jump': None, 'ThirdPerson': 'original project retargeted graph; unresolved'},
}
for source, dest in [
    ('import_manifest_v3.json', 'RepairImportManifest.json'),
    ('repair_pose_check.json', 'RepairPoseChecks.json'),
    ('pie_validation.json', 'RepairPIEValidation.json'),
    ('visual_validation.json', 'RepairVisualValidation.json'),
    ('source_audit.json', 'RepairSourceAudit.json'),
    ('repair_configure_complete.json', 'RepairConfiguration.json'),
    ('repair_build_final.txt', 'RepairBuild.txt'),
]:
    shutil.copyfile(saved / source, docs / dest)
(docs / 'RepairSummary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
screenshots = docs / 'RepairScreenshots'
screenshots.mkdir(exist_ok=True)
for path in saved.glob('view_*.png'):
    shutil.copyfile(path, screenshots / path.name)
print(json.dumps(summary, ensure_ascii=False, indent=2))
