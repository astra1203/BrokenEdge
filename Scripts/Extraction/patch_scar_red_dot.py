"""Recalibrate the red dot on the SCAR-H (2026-11-04 user report: no dot visible, wrong seat).

Measured on SCARH_Candidate_1P / _3P (same frame, +Y forward, +Z up, |X| < 1 cm):
    top rail Z = 14.29 for Y 12..34; a cut at Y 10..12 (old mount Y=10.921 hung over it)
    rear sight ReaS_ScarH_024 peaks Z = 17.8 at Y ~0, front sight FroS_ScarH_015 Z = 18.8 at Y ~36
The lens centre sits 1.54 cm above the mount, so with the sights up the aim line went
straight through both sight bodies. ExtractionWeaponVisuals now hides the FroS_/ReaS_
sections while an optic is fitted (folded sights); this script reseats the optic on the
continuous rail and gives SCAR-H its own first person mount and aim offset, computed with
the SCAR presentation aim fit the same way patch_m4a1_attachments.py does for M4A1.
M4A1 sits 0.32 cm below its rail top (11.22 -> 10.904); SCAR uses the same seat depth.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/patch_scar_red_dot.py" -NullRHI -unattended -nosound
"""
import json
import sys
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir())
sys.path.insert(0, str(ROOT / 'Scripts/Extraction'))
from optic_calibration import settings, describe  # noqa: E402

WEAPON = 'SCARH'
MOUNT = (0.0, 18.0, 14.29 - 0.32)
PRESENTATION = '/Game/Extraction/Weapons/SCARH/DA_SCARH_Presentation'
ASSET = '/Game/Extraction/Items/DA_Item_RedDot'


def merged(asset, prop, value):
    result = dict(asset.get_editor_property(prop) or {})
    result[unreal.Name(WEAPON)] = value
    return result


aim = unreal.load_asset(PRESENTATION).get_editor_property('view_fit').get_editor_property('aim')
dot = unreal.load_asset(ASSET)
assert dot, ASSET
optic = settings(aim, MOUNT)
dot.set_editor_property('mount_by_weapon', merged(dot, 'mount_by_weapon', optic['mount']))
dot.set_editor_property('first_person_mount_by_weapon',
                        merged(dot, 'first_person_mount_by_weapon', optic['first_person_mount']))
dot.set_editor_property('optic_aim_offset_by_weapon',
                        merged(dot, 'optic_aim_offset_by_weapon', optic['optic_aim_offset']))
assert unreal.EditorAssetLibrary.save_loaded_asset(dot, only_if_is_dirty=False)
report = {'weapon': WEAPON, 'mount_cm': list(MOUNT), 'optic': describe(optic)}
out = ROOT / 'Saved/Extraction/SCARRedDot.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('SCAR_RED_DOT_PATCHED', json.dumps(report, ensure_ascii=False))
