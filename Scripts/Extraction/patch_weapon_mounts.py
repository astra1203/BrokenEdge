"""Give the SCAR-H its own attachment placement, leaving the AK numbers untouched.

`Mount` on an attachment is a single transform, so it can only ever be right for
one weapon frame. The definition carries `MountByWeapon` /
`FirstPersonMountByWeapon`, keyed by host weapon item id, which
`UExtractionItemDefinition::Placement` consults before falling back to `Mount`.

That override lives in the C++ class, so this script only works against a module
built after the fields were added. If the running editor still holds the old
binaries the property is absent from reflection and every write fails; the script
detects that and says so instead of throwing a traceback.

Values come from the source gunsmith socket table (`df_socket_mounts.py`), not
from eyeballing: the red dot sits on the receiver's `Sco_point`, the vertical
grip on the handguard's `UnderBar_point`, the compensator on the barrel's
`Muzzle_point`. Run inside the editor:

    py "D:/Users/SCSJ/BrokenEdge2.0/Scripts/Extraction/patch_weapon_mounts.py"

    # Raise only the optic by 2.2 cm; the socket is the rail surface, so the
    # default value leaves the sight resting flush on it.
    py ".../patch_weapon_mounts.py" --lift 2.2

    # Only if the override fields are not compiled in yet: rewrite the shared
    # Mount / FirstPersonMount instead. This also moves the AK's attachments,
    # because the three definitions are shared by every weapon.
    py ".../patch_weapon_mounts.py" --direct
"""
import argparse
import sys
from pathlib import Path

import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import df_socket_mounts

WEAPON = 'SCARH'
ITEM_FOLDER = '/Game/Extraction/Items'
SLOT_FOR_ITEM = {'RedDot': 'Optic', 'VerticalGrip': 'ForeGrip', 'Compensator': 'Muzzle'}


def has_override(asset):
    """True when the loaded class carries MountByWeapon (i.e. the module is built)."""
    try:
        asset.get_editor_property('mount_by_weapon')
        return True
    except Exception:
        return False


def moved(transform, location):
    """The same transform with a new translation, rotation and scale untouched."""
    try:
        transform.translation = location
    except Exception:
        transform.set_editor_property('translation', location)
    return transform


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--direct', action='store_true',
                        help='write the shared Mount fields instead of a per-weapon override')
    parser.add_argument('--lift', type=float, default=0.0,
                        help='raise every attachment by this many cm on top of the socket value; '
                             'the socket is the rail surface, so 0 leaves the part flush on the rail')
    args = parser.parse_args()

    library = unreal.EditorAssetLibrary
    rows = df_socket_mounts.load()['Rows']
    values = {slot: df_socket_mounts.to_frame(
                  df_socket_mounts.resolve(rows, 'Rec_AR_ScarH_045_1P', mesh, socket), 'raw')
              for slot, (mesh, socket) in df_socket_mounts.SLOTS.items()}
    if args.lift:
        # Optic only: the muzzle and grip sockets are where those parts physically
        # seat, so lifting them would tear them off the barrel and handguard.
        v = values['Optic']
        values['Optic'] = (v[0], v[1], v[2] + args.lift)

    assets = {}
    for item in SLOT_FOR_ITEM:
        path = '%s/DA_Item_%s' % (ITEM_FOLDER, item)
        asset = unreal.load_asset(path)
        if not asset:
            raise SystemExit('missing asset ' + path)
        assets[item] = asset

    override = has_override(next(iter(assets.values())))
    if not override and not args.direct:
        raise SystemExit(
            'mount_by_weapon is not on %s: the module running in this editor was built '
            'before the field existed. Compile, restart the editor, then run this again.\n'
            'To write the shared Mount fields right now instead, pass --direct '
            '(the AK shares these three definitions and will move too).'
            % type(next(iter(assets.values()))).__name__)

    report = {}
    for item, asset in assets.items():
        slot = SLOT_FOR_ITEM[item]
        location = unreal.Vector(*values[slot])
        # Copy the authored transforms and move only the translation: the rotation
        # and scale were never weapon specific, and FTransform.rotation comes back
        # as a Quat, which unreal.Transform() will not accept as a Rotator.
        mount = asset.get_editor_property('mount')
        view = asset.get_editor_property('first_person_mount')
        was = [round(mount.translation.x, 3), round(mount.translation.y, 3),
               round(mount.translation.z, 3)]
        mount = moved(mount, location)
        view = moved(view, location)
        if override:
            asset.set_editor_property('mount_by_weapon', {unreal.Name(WEAPON): mount})
            asset.set_editor_property('first_person_mount_by_weapon', {unreal.Name(WEAPON): view})
        else:
            asset.set_editor_property('mount', mount)
            asset.set_editor_property('first_person_mount', view)
        if not library.save_loaded_asset(asset):
            raise SystemExit('save failed for ' + item)
        report[item] = {'socket': df_socket_mounts.SLOTS[slot][1],
                        'was_cm': was,
                        'mount_cm': [round(v, 3) for v in values[slot]]}

    print('WEAPON_MOUNTS_PATCHED', 'override' if override else 'direct', report)


if __name__ == '__main__':
    main()
