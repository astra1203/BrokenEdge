"""Recorded isolated editor tasks for Phase 3 assets and inventory rule tests."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Common.ue_env import engine_root, run_record
parser = argparse.ArgumentParser()
parser.add_argument('task', choices=['inventory-net-pie', 'vitals-net-pie', 'runtime-assets', 'aim-pie', 'automation-list', 'native-blueprints', 'combat-net-pie', 'hud-profile', 'generated-widgets', 'inventory-widgets', 'extraction-widgets', 'widgets-validate', 'item-registry', 'item-registry-create', 'item-registry-export', 'presentation', 'presentation-validate', 'presentation-migrate', 'presentation-audit', 'combat', 'combat-pie', 'sprint-pie', 'vitals-pie', 'vitals', 'input-pie', 'input-assets', 'input', 'input-audit', 'capacity', 'respawn', 'prototype-audit', 'spawn-lifecycle', 'exit-feedback', 'guard-bt', 'loot-layout', 'weapon-range-assets', 'weapon-pie', 'gunsmith-assets', 'weapon-mechanics', 'weapon-names', 'assets', 'exit', 'inventory', 'persistence', 'recover-existing', 'phase5-assets', 'phase6a-assets', 'optic-assets', 'equipment', 'phase6b-assets', 'progression', 'north-import', 'phase7a-assets', 'phase7a-audit'])
parser.add_argument('--tag', required=True)
parser.add_argument('--engine', type=Path)
parser.add_argument('--force', action='store_true', help='Explicitly overwrite changed/untracked generated widgets')
parser.add_argument('--store', type=Path, help='Existing store directory; only for recover-existing')
parser.add_argument('--verify-profile')
parser.add_argument('--verify-item')
args = parser.parse_args()
if args.force and args.task not in ('inventory-widgets', 'extraction-widgets'):
    parser.error('--force is only valid for inventory-widgets or extraction-widgets')
try:
    ENGINE = engine_root(args.engine)
except (ValueError, OSError, KeyError) as exc:
    parser.error(str(exc))
assert args.tag and all(c.isalnum() or c in '-_' for c in args.tag)
out = ROOT / 'Saved/Extraction' / args.tag
out.mkdir(parents=True, exist_ok=False)
command = [str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'), str(ROOT / 'BrokenEdge.uproject'),
           '-unattended', '-nosplash', '-nosound', '-NoLoadingScreen', '-NullRHI', '-abslog=' + str(out / 'Editor.log')]
if args.force:
    command += ['-ForceGeneratedWidgets']
if args.task == 'automation-list':
    command += ['-ExecCmds=Automation SetFilter All;List', '-TestExit=Automation Test Queue Empty']
elif args.task == 'native-blueprints':
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Common/audit_native_blueprints.py')]
elif args.task == 'capacity':
    command += ['-ExecCmds=Automation SetFilter Stress;RunTests BrokenEdge.Extraction.Persistence.Capacity',
                '-TestExit=Automation Test Queue Empty', '-ReportExportPath=' + str(out / 'Automation')]
elif args.task == 'respawn':
    command += ['-ExecCmds=Automation RunTests BrokenEdge.GameMode.Respawn.',
                '-TestExit=Automation Test Queue Empty', '-ReportExportPath=' + str(out / 'Automation')]
elif args.task in ['inventory-widgets', 'extraction-widgets', 'widgets-validate']:
    script = {'inventory-widgets': 'create_inventory_widgets.py', 'extraction-widgets': 'create_extraction_blueprints.py', 'widgets-validate': 'validate_generated_widgets.py'}[args.task]
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction' / script)]
elif args.task in ['item-registry-create', 'item-registry-export']:
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/item_registry.py')]
elif args.task in ['presentation-audit', 'presentation-migrate', 'presentation-validate']:
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Stinger/character_presentation.py')]
elif args.task == 'input-assets':
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/prepare_input_actions.py')]
elif args.task == 'input-audit':
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/audit_input_actions.py')]
elif args.task == 'prototype-audit':
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Common/audit_prototype_references.py')]
elif args.task == 'weapon-range-assets':
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/prepare_weapon_range.py')]
elif args.task == 'gunsmith-assets':
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/configure_gunsmith.py')]
elif args.task == 'weapon-names':
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/configure_weapon_mechanics.py')]
elif args.task in ['north-import', 'phase7a-assets', 'phase7a-audit']:
    script = {'north-import': 'Scripts/SpaceCenter/import_sample.py', 'phase7a-assets': 'Scripts/Extraction/prepare_phase7a.py', 'phase7a-audit': 'Scripts/Extraction/audit_phase7a.py'}[args.task]
    command += ['-run=pythonscript', '-script=' + str(ROOT / script)]
elif args.task == 'phase6b-assets':
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/prepare_phase6b.py')]
elif args.task == 'optic-assets':
    script = ROOT / 'Scripts/Extraction/calibrate_red_dot.py'
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    str(ROOT / 'Scripts/Assert-EditorClosed.ps1'), '-ProjectFile', str(ROOT / 'BrokenEdge.uproject')], check=True)
    command += ['-run=pythonscript', '-script=' + str(script)]
elif args.task == 'phase6a-assets':
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction/prepare_phase6a.py')]
elif args.task in ['assets', 'exit', 'phase5-assets']:
    command += ['-run=pythonscript', '-script=' + str(ROOT / 'Scripts/Extraction' / ('prepare_phase5.py' if args.task == 'phase5-assets' else 'prepare_exit.py' if args.task == 'exit' else 'prepare_items.py'))]
else:
    test = {'inventory-net-pie': 'NetworkInventoryPIE.', 'vitals-net-pie': 'VitalsNetworkPIE.', 'runtime-assets': 'RuntimeAssets.', 'aim-pie': 'WeaponPIE.ThirdPersonAimPose', 'combat-net-pie': 'CombatNetworkPIE.', 'hud-profile': 'HUDProfile.', 'generated-widgets': 'GeneratedWidgets.', 'item-registry': 'ItemRegistry.', 'presentation': 'Presentation.', 'combat': 'CombatMode.', 'combat-pie': 'WeaponPIE.InputAndAssembly', 'sprint-pie': 'WeaponPIE.SprintTransitions', 'vitals-pie': 'VitalsPIE.', 'vitals': 'Vitals.', 'input-pie': 'InputPIE', 'input': 'Input.', 'spawn-lifecycle': 'SpawnLifecycle', 'exit-feedback': 'ExitFeedback', 'guard-bt': 'GuardBT', 'loot-layout': 'LootLayout', 'weapon-pie': 'WeaponPIE', 'weapon-mechanics': 'WeaponMechanics', 'persistence': 'Persistence', 'inventory': 'Inventory', 'recover-existing': 'RecoverExistingStore', 'equipment': 'Equipment', 'progression': 'Progression'}[args.task]
    if args.task in ['inventory-net-pie', 'vitals-net-pie', 'runtime-assets', 'aim-pie', 'combat-net-pie', 'hud-profile', 'weapon-pie', 'input-pie', 'vitals-pie', 'combat-pie', 'sprint-pie']:
        command[0] = str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor.exe')
        command.remove('-NullRHI')
        command += ['-RenderOffscreen', '-ResX=1280', '-ResY=720']
    if args.task == 'runtime-assets':
        command.insert(2, '/Game/Extraction/Training/L_WeaponRange')
        # Standalone game validation uses only C++ automation. Editor Python
        # startup toolsets depend on editor-only types unavailable with -game.
        command += ['-game', '-DisablePython', '-RuntimeAssetsScreenshot=' + str(out / 'Inventory.png')]
    if args.task == 'hud-profile':
        command += ['-trace=cpu,frame,bookmark,counters', '-tracefile=' + str(out / 'HUD.utrace')]
    if args.task == 'recover-existing':
        if not args.store or not (args.store / 'Store.journal').is_file():
            raise ValueError('An explicit existing journal is required')
        command += ['-ExtractionRecoveryStore=' + str(args.store.resolve())]
        if args.verify_profile:
            if not args.verify_item: raise ValueError('Expected AI item is required with profile')
            command += ['-ExtractionVerifyProfile=' + args.verify_profile, '-ExtractionVerifyItem=' + args.verify_item]
    command += ['-ExecCmds=Automation RunTests BrokenEdge.Extraction.' + test,
                '-TestExit=Automation Test Queue Empty', '-ReportExportPath=' + str(out / 'Automation')]
metadata = {'task': args.task, 'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'command': command}
metadata.update(run_record(command, out / 'Run.json', engine=ENGINE, script=__file__))
environment = os.environ.copy()
environment['UE_SKIP_UBT_SDK_SETUP'] = '1'
environment['EXTRACTION_RUN_DIR'] = str(out)
if args.task.startswith('item-registry-'):
    environment['ITEM_REGISTRY_MODE'] = args.task
if args.task.startswith('presentation-'):
    environment['CHARACTER_PRESENTATION_MODE'] = args.task
if args.task == 'north-import':
    environment.update(SPACECENTER_WORK=str(ROOT / 'Saved/SpaceCenter/Phase7A'), SPACECENTER_ASSETS_ONLY='1')
with (out / 'Process.log').open('wb') as log:
    proc = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    metadata['pid'] = proc.pid
    (out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print('EDITOR_TASK_STARTED', proc.pid, str(out), flush=True)
    try:
        metadata['exit_code'] = proc.wait(timeout=600)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], capture_output=True)
        metadata['exit_code'] = proc.wait(timeout=30)
        metadata['error'] = 'Editor task timed out'
text = (out / 'Editor.log').read_text(encoding='utf-8', errors='replace')
passed = metadata['exit_code'] == 0 and 'Fatal error:' not in text and 'Traceback (most recent call last)' not in text
if args.task == 'automation-list':
    names = sorted(set(re.findall(r"LogAutomationCommandLine: Display:\s*'(BrokenEdge\.[^']+)'", text)))
    metadata['discovered_tests'] = len(names)
    (out / 'AutomationList.json').write_text(json.dumps(names, indent=2) + '\n', encoding='utf-8')
    passed = passed and bool(names) and 'Automation Test Queue Empty' in text
elif args.task == 'native-blueprints':
    passed = passed and 'NATIVE_BLUEPRINT_AUDIT_COMPLETE' in text
elif args.task in ['inventory-widgets', 'extraction-widgets', 'widgets-validate']:
    marker = {'inventory-widgets': 'INVENTORY_WIDGETS PASSED', 'extraction-widgets': 'EXTRACTION_BLUEPRINTS_COMPLETE', 'widgets-validate': 'GENERATED_WIDGETS_VALIDATION_COMPLETE'}[args.task]
    passed = passed and marker in text
elif args.task in ['item-registry-create', 'item-registry-export']:
    passed = passed and 'ITEM_REGISTRY_COMPLETE' in text
elif args.task == 'presentation-validate':
    passed = passed and 'CHARACTER_PRESENTATION_VALIDATION_COMPLETE' in text
elif args.task == 'presentation-migrate':
    passed = passed and 'CHARACTER_PRESENTATION_MIGRATION_COMPLETE' in text
elif args.task == 'presentation-audit':
    passed = passed and 'CHARACTER_PRESENTATION_AUDIT_COMPLETE' in text
elif args.task == 'input-assets':
    passed = passed and 'EXTRACTION_INPUT_ASSETS_COMPLETE' in text
elif args.task == 'input-audit':
    passed = passed and 'INPUT_ACTION_AUDIT_COMPLETE' in text
elif args.task == 'prototype-audit':
    passed = passed and 'PROTOTYPE_REFERENCE_AUDIT_COMPLETE' in text
elif args.task in ['north-import', 'phase7a-assets', 'phase7a-audit']:
    marker = {'north-import': 'SPACE_REGION_ASSETS_COMPLETE', 'phase7a-assets': 'EXTRACTION_PHASE7A_ASSETS_PREPARED', 'phase7a-audit': 'EXTRACTION_PHASE7A_AUDIT_PASSED'}[args.task]
    passed = passed and marker in text
elif args.task == 'weapon-range-assets':
    passed = passed and 'EXTRACTION_WEAPON_RANGE_PREPARED' in text
elif args.task == 'gunsmith-assets':
    passed = passed and 'EXTRACTION_GUNSMITH_PREPARED' in text
elif args.task == 'weapon-names':
    passed = passed and 'EXTRACTION_WEAPON_NAMES_PREPARED' in text
elif args.task == 'phase6b-assets':
    passed = passed and 'EXTRACTION_PHASE6B_ASSETS_PREPARED' in text
elif args.task == 'optic-assets':
    passed = passed and 'EXTRACTION_OPTIC_PREPARED' in text
elif args.task == 'phase6a-assets':
    passed = passed and 'EXTRACTION_PHASE6A_ASSETS_PREPARED' in text
elif args.task in ['assets', 'exit', 'phase5-assets']:
    passed = passed and ('EXTRACTION_PHASE5_ASSETS_PREPARED' if args.task == 'phase5-assets' else 'EXTRACTION_PHASE4_EXIT_PREPARED' if args.task == 'exit' else 'EXTRACTION_PHASE3_ASSETS_PREPARED') in text
else:
    successes = re.findall(r'Test Completed\. Result=\{(?:Success|\u6210\u529f)\}', text)
    metadata['successful_tests'] = len(successes)
    expected = {'inventory-net-pie': 1, 'vitals-net-pie': 1, 'runtime-assets': 1, 'aim-pie': 1, 'combat-net-pie': 1, 'hud-profile': 1, 'generated-widgets': 1, 'item-registry': 1, 'presentation': 1, 'combat': 4, 'combat-pie': 1, 'sprint-pie': 1, 'vitals-pie': 1, 'vitals': 1, 'input-pie': 1, 'input': 1, 'capacity': 1, 'respawn': 1, 'spawn-lifecycle': 3, 'exit-feedback': 3, 'guard-bt': 12, 'loot-layout': 1, 'weapon-pie': 4, 'weapon-mechanics': 5, 'persistence': 6, 'inventory': 7, 'recover-existing': 1, 'equipment': 4, 'progression': 7}[args.task]
    passed = passed and len(successes) == expected and not re.search(r'Test Completed\. Result=\{(?:Fail|Failure|\u5931\u8d25)', text)
if passed and args.task in ['inventory-widgets', 'extraction-widgets']:
    reload_tag = args.tag + '_reload'
    reload_command = [sys.executable, __file__, 'widgets-validate', '--engine', str(ENGINE), '--tag', reload_tag]
    metadata['reload_validation'] = str(ROOT / 'Saved/Extraction' / reload_tag / 'Run.json')
    passed = subprocess.run(reload_command, cwd=ROOT).returncode == 0
metadata['result'] = 'passed' if passed else 'failed'
metadata['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(out / 'Run.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'result': metadata['result'], 'exit_code': metadata['exit_code'], 'successful_tests': metadata.get('successful_tests')}, ensure_ascii=True), flush=True)
if not passed:
    errors = {}
    for line in text.splitlines():
        if 'Error:' in line or 'Traceback' in line or 'Exception' in line:
            message = re.sub(r'^\[[^\]]+\]\[\s*\d+\]', '', line)
            errors[message] = errors.get(message, 0) + 1
    for message, count in list(errors.items())[:40]:
        print((message + (f' [repeated {count} times]' if count > 1 else '')).encode('ascii', errors='backslashreplace').decode(), flush=True)
    if len(errors) > 40:
        print(f'{len(errors) - 40} additional unique errors; full log: {out / "Editor.log"}', flush=True)
sys.exit(0 if passed else 1)
