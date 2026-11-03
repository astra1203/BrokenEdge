"""Import the third-person jump set converted by prepare_jump.py and bind it to DA_RifleLocomotion.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Stinger/import_jump.py" -NullRHI -unattended -nosound

Uses the same limb-length fixed mapping as the locomotion import, so jump and
locomotion poses share bone lengths. Also makes every rifle presentation that has
no locomotion set (SCAR-H) use the shared official set.
"""
import json
import os
import sys
from pathlib import Path

import unreal

project = Path(unreal.Paths.project_dir())
work = project / 'Saved/Stinger/Jump'
results = json.loads((work / 'export/ExportResults.json').read_text(encoding='utf-8'))
assert results and all(r['Success'] for r in results), 'Incomplete source conversion'
DEST = '/Game/Characters/Stinger/Animations/DeltaForceLocomotion'
os.environ['STINGER_REPAIR_SOURCE'] = str(work / 'export')
os.environ['STINGER_REPAIR_DEST'] = DEST
os.environ['STINGER_REPAIR_REPORT'] = str(work / 'Import.json')
os.environ['STINGER_REPAIR_NAMES'] = '*'
os.environ['STINGER_REPAIR_FIXED_LIMBS'] = '1'
sys.path.insert(0, str(project / 'Scripts/Stinger'))
import repair_import  # noqa: E402
repair_import.main()
manifest = json.loads((work / 'Import.json').read_text(encoding='utf-8'))
assert all(e['status'] == 'written' for e in manifest['entries'].values())

lib = unreal.EditorAssetLibrary


def mapped(name):
    hits = [p for p in lib.list_assets(DEST + '/MappedV4', recursive=True) if p.rsplit('/', 1)[-1].split('.')[0].lower() == name.lower()]
    assert hits, name
    asset = unreal.load_asset(hits[0])
    assert isinstance(asset, unreal.AnimSequence), name
    return asset


SET = DEST + '/DA_RifleLocomotion'
movement = unreal.load_asset(SET)
bindings = {
    'jump_start': 'AR_M_3P_StandJumpstart', 'jump_loop': 'AR_M_3P_StandJumpLoop', 'jump_land': 'AR_M_3P_StandJumpLand',
    'run_jump_start': 'AR_M_3P_RunJumpstart', 'run_jump_loop': 'AR_M_3P_RunJumploop',
    'run_jump_land': 'AR_M_3P_RunJumpLand', 'fall_loop': 'AR_M_3P_FallLoopH',
}
for prop, name in bindings.items():
    movement.set_editor_property(prop, mapped(name))
assert lib.save_loaded_asset(movement, only_if_is_dirty=False)

shared = []
for path in ['/Game/Extraction/Weapons/SCARH/DA_SCARH_Presentation', '/Game/Extraction/Weapons/M4A1/DA_M4A1_Presentation']:
    presentation = unreal.load_asset(path)
    if presentation and not presentation.get_editor_property('locomotion'):
        presentation.set_editor_property('locomotion', movement)
        assert lib.save_loaded_asset(presentation, only_if_is_dirty=False)
        shared.append(path)
print('JUMP_IMPORTED', len(manifest['entries']), 'locomotion_shared_to', shared)
