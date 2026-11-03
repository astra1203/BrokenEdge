"""Open the M4A1 to the existing red dot / vertical grip / compensator.

`build_weapon.py` authored `compatible_attachments=[]` for M4A1 because no mount was
calibrated for its frame. This script writes M4A1 entries into the per-weapon maps
of the three shared attachment definitions; AK and SCAR-H entries stay untouched.

Mount values are the official gunsmith sockets of the default preset 10010000122
(`WeaponPartsMeshSocketNameInfo`, chained receiver -> handguard / barrel exactly as
`prepare_weapon.assemble` does), in the raw frame the generic pipeline imports:
    Optic   Rec_AR_M4A1-Std_001  Sco_point       (0, 8.195, 10.904)
    Grip    Han_AR15_mk18_042    UnderBar_point  (0, 31.299, 5.668)
    Muzzle  Bar_S-370_M4A1-Std   Muzzle_point    (-0.014, 50.264, 8.07)
Recompute with `df_socket_mounts`-style resolution if the preset changes.

The first person optic mount and aim offset use `optic_calibration.settings` with the
M4A1 presentation aim fit, so the lens centre lands on the camera ray. The support
hand keeps the AK grip's calibrated wrist offset relative to the grip mount. M4A1 and
SCAR-H have their own rail, so the AK riser adapter is suppressed on both.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/patch_m4a1_attachments.py" -NullRHI -unattended -nosound
"""
import json
import sys
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir())
sys.path.insert(0, str(ROOT / 'Scripts/Extraction'))
from optic_calibration import settings, describe  # noqa: E402

WEAPON = 'M4A1'
RAIL_WEAPONS = ['SCARH', 'M4A1']
ITEMS = '/Game/Extraction/Items/DA_Item_'
PRESENTATION = '/Game/Extraction/Weapons/M4A1/DA_M4A1_Presentation'
MOUNTS = {'RedDot': (0.0, 8.195, 10.904), 'VerticalGrip': (0.0, 31.299, 5.668),
          'Compensator': (-0.014, 50.264, 8.07)}
lib = unreal.EditorAssetLibrary


def load(path):
    asset = unreal.load_asset(path)
    assert asset, 'missing ' + path
    return asset


def merged(asset, prop, value):
    """Copy of a per-weapon map with only the M4A1 key replaced."""
    result = dict(asset.get_editor_property(prop) or {})
    result[unreal.Name(WEAPON)] = value
    return result


aim = load(PRESENTATION).get_editor_property('view_fit').get_editor_property('aim')
report = {'weapon': WEAPON, 'parts': {}}
for item, location in MOUNTS.items():
    asset = load(ITEMS + item)
    try:
        asset.get_editor_property('optic_aim_offset_by_weapon')
    except Exception:
        raise SystemExit('Per-weapon grip/optic fields are missing: build the editor module first.')
    mount = unreal.Transform(location=unreal.Vector(*location))
    view = mount
    if item == 'RedDot':
        optic = settings(aim, location)
        view = optic['first_person_mount']
        asset.set_editor_property('optic_aim_offset_by_weapon',
                                  merged(asset, 'optic_aim_offset_by_weapon', optic['optic_aim_offset']))
        report['parts'][item] = describe(optic)
    if item == 'VerticalGrip':
        legacy = asset.get_editor_property('mount').translation
        grip = asset.get_editor_property('support_grip')
        shift = unreal.Vector(*location) - legacy
        grip = unreal.Transform(location=grip.translation + shift, rotation=grip.rotation.rotator(),
                                scale=grip.scale3d)
        asset.set_editor_property('support_grip_by_weapon', merged(asset, 'support_grip_by_weapon', grip))
        report['parts'][item] = describe({'support_grip': grip})
    asset.set_editor_property('mount_by_weapon', merged(asset, 'mount_by_weapon', mount))
    asset.set_editor_property('first_person_mount_by_weapon', merged(asset, 'first_person_mount_by_weapon', view))
    free = [str(n) for n in asset.get_editor_property('adapter_free_weapons')]
    asset.set_editor_property('adapter_free_weapons',
                              [unreal.Name(n) for n in dict.fromkeys(free + RAIL_WEAPONS)])
    report['parts'].setdefault(item, {})['mount_cm'] = list(location)
    assert lib.save_loaded_asset(asset, only_if_is_dirty=False), item

gun = load(ITEMS + WEAPON)
gun.set_editor_property('compatible_attachments', [unreal.Name(n) for n in MOUNTS])
assert lib.save_loaded_asset(gun, only_if_is_dirty=False)
report['compatible_attachments'] = [str(n) for n in gun.get_editor_property('compatible_attachments')]
out = ROOT / 'Saved/Extraction/M4A1Attachments.json'
out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('M4A1_ATTACHMENTS_PATCHED', json.dumps(report, ensure_ascii=False))
