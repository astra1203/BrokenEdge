"""Archive a completed phase-0 run, its logs, and separately reviewed screenshots."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('--lan', type=Path, required=True)
parser.add_argument('--visual-review', type=Path, required=True)
parser.add_argument('--prefix', default='phase0')
args = parser.parse_args()
project = Path(__file__).resolve().parents[2]
saved = project / 'Saved/Stinger/Baseline'
destination = project / 'Artifacts/Stinger/BaselineEvidence'
suites = {'audit': 'Audit.json', 'combat': 'Combat.json', 'actions': 'pie_validation.json'}
for players in [1, 2]:
    suites[f'grip-{players}p'] = f'third_person_grip_p0_{players}p.json'
    suites[f'grip-{players}p-visual'] = f'third_person_grip_p0_{players}p.json'
    suites[f'visibility-{players}p'] = f'weapon_visibility_{players}p_stills.json'
summary = {'scope': 'UE 5.8.3 phase 0: same-process PIE gameplay and separate-process LAN lobby flow',
           'suites': [], 'lan_tests': [], 'files': [], 'inputs': []}
copies = []
for suite, filename in suites.items():
    run = saved / (args.prefix + '-' + suite)
    report = json.loads((run / filename).read_text(encoding='utf-8-sig'))
    metadata = json.loads((run / 'Run.json').read_text(encoding='utf-8-sig'))
    assert report['result'] == 'passed' and metadata['exit_code'] == 0, suite
    assert report['checks'] and all(check['passed'] for check in report['checks']), suite
    summary['suites'].append({'name': suite, 'checks': len(report['checks']),
                             'samples': len(report.get('samples', [])), 'report': suite + '/' + filename})
    copies += [(run / filename, suite + '/' + filename), (run / 'Run.json', suite + '/Run.json'),
               (run / 'Editor.log', suite + '/Editor.txt')]
lan = args.lan.resolve()
for name in ['Empty'] + [role + '-' + mode for mode in ['FreeForAll', 'Teams'] for role in ['Server', 'Client1', 'Client2']]:
    path = lan / (name + '.log')
    log = path.read_text(encoding='utf-8-sig')
    assert re.search(r'Test Completed\. Result=\{(成功|Success)\}', log), name
    assert not re.search(r'Test Completed\. Result=\{(失败|Fail|Failed)\}|Fatal error:', log), name
    summary['lan_tests'].append({'name': name, 'report': 'LAN/' + name + '.txt',
                                 'events': re.findall(r'^.*(?:SESSION_FLOW_OK|MODE_SWITCH_OK|READY_CANCEL_AND_RETRY_OK).*$', log, re.M)})
for path in lan.glob('*.log'):
    copies.append((path, 'LAN/' + path.stem + '.txt'))
review = json.loads(args.visual_review.read_text(encoding='utf-8-sig'))
assert review['result'] == 'passed' and review['images']
for item in review['images']:
    path = project / item['source']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == item['sha256'], path
    copies.append((path, 'Captures/' + item['archive_name']))
copies += [(args.visual_review, 'VisualReview.json'), (saved / 'audit-initial/Audit.json', 'Diagnostics/InitialAudit.json'),
           (saved / 'grip-1p/SetupDiagnosis.json', 'Diagnostics/PIESetup.json'),
           (saved / 'pickup-runtime/weapon_visibility_1p_stills.json', 'Diagnostics/PickupBeforeFix.json')]
for source, relative in copies:
    assert source.is_file(), source
destination.mkdir(parents=True, exist_ok=False)
for source, relative in copies:
    target = destination / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
for name in ['build-pickup.txt']:
    path = saved / name
    data = path.read_bytes()
    (destination / name).write_text(data.decode('utf-16' if data.startswith(b'\xff\xfe') else 'utf-8-sig'), encoding='utf-8')
input_paths = set()
for pattern in ['Source/BrokenEdge/UI/BrokenEdgeHUD.cpp', 'Source/BrokenEdge/Weapon/Weapon.cpp', 'Source/StingerTools/Private/StingerAssetLibrary.cpp',
                'Config/DefaultGame.ini', 'Scripts/Stinger/*baseline*.py', 'Scripts/Stinger/validate_*.py',
                'Scripts/Stinger/configure_third_person_grip.py', 'Scripts/Stinger/calibrate_support_hand.py',
                'Content/Characters/Stinger/Animations/ThirdPerson/ABP_BrokenEdgeAnimInstance_Stinger.uasset',
                'Content/Characters/Stinger/Animations/ThirdPerson/Idle_Rifle_*Retargeted_Stinger.uasset',
                'Content/Characters/Stinger/Animations/ThirdPerson/Crouch_Idle_Rifle_*Retargeted_Stinger.uasset',
                'Content/Characters/Stinger/Animations/ThirdPerson/Crouch_Walk_*_Rifle_*Retargeted_Stinger.uasset',
                'Content/Characters/Stinger/Animations/ThirdPerson/Walk_*_Rifle_Ironsights_Retargeted_Stinger.uasset',
                'Content/Characters/Stinger/Animations/ThirdPerson/Jog_*_Rifle_Retargeted_Stinger.uasset',
                'Content/FPS_Weapon_Bundle/Weapons/Meshes/Ka47/SK_KA47.uasset']:
    for path in sorted(project.glob(pattern)):
        input_paths.add(path)
for path in sorted(input_paths):
    summary['inputs'].append({'path': path.relative_to(project).as_posix(), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
for path in sorted(destination.rglob('*')):
    if path.is_file():
        summary['files'].append({'path': path.relative_to(destination).as_posix(), 'bytes': path.stat().st_size,
                                 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
summary['result'] = 'passed'
(destination / 'Summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('BASELINE_ARCHIVED', destination, len(summary['files']), 'files')
