"""Add the bolt-action sniper rifle, pump shotgun and pistol (项目自定数值).

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/configure_extra_weapons.py" -NullRHI -unattended -nosound

Models come from the project's Military Weapons Silver pack (/Game/MilitaryWeapSilver, Epic sample content),
not from the unpacked game data. Each gun duplicates BP_AK47 (same hit-scan pipeline and rifle presentation),
swaps in the pack mesh and sets its own magazine, fire interval, damage and fire mode; each gets its own ammo
definition. Attachments stay disabled until mount points are calibrated. Idempotent.
"""
import json

import unreal

lib = unreal.EditorAssetLibrary
ITEMS = '/Game/Extraction/Items'

# id, label, mesh, magazine, fire interval s, damage vs AK, automatic, scatter, footprint, rarity, sell value,
# ammo id, ammo label, description
WEAPONS = [
    ('BoltSniper', '栓动狙击步枪', '/Game/MilitaryWeapSilver/Weapons/Sniper_Rifle_A', 5, 1.3, 3.0, False, False,
     (6, 2), 4, 9000, 'Ammo_338', '.338 狙击弹', '单发高伤害的栓动步枪，适合远距离。'),
    ('PumpShotgun', '泵动霰弹枪', '/Game/MilitaryWeapSilver/Weapons/Shotgun_A', 6, .9, 2.2, False, True,
     (5, 2), 3, 5000, 'Ammo_12G', '12 号霰弹', '近距离威力大，散布明显。'),
    ('Pistol9', '半自动手枪', '/Game/MilitaryWeapSilver/Weapons/Pistols_A', 15, .18, .7, False, False,
     (2, 2), 1, 1500, 'Ammo_9mm', '9mm 手枪弹', '轻便的副武器。'),
]


def duplicate(source, target):
    if lib.does_asset_exist(target):
        return unreal.load_asset(target)
    asset = lib.duplicate_asset(source, target)
    assert asset, target
    return asset


ak_bp = unreal.load_asset('/Game/Blueprints/Weapon/BP_AK47')
ak = unreal.get_default_object(ak_bp.generated_class())
report = []
for (ident, label, mesh_path, magazine, interval, scale, automatic, scatter, size, rarity, value,
     ammo_id, ammo_label, description) in WEAPONS:
    mesh = unreal.load_asset(mesh_path)
    assert isinstance(mesh, unreal.SkeletalMesh), mesh_path
    sockets = [mesh.get_socket_by_index(i).get_editor_property('socket_name') for i in range(mesh.num_sockets())]
    bp = duplicate('/Game/Blueprints/Weapon/BP_AK47', '/Game/Extraction/Weapons/%s/BP_%s' % (ident, ident))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    component = cdo.get_editor_property('weapon_mesh')
    component.set_skeletal_mesh_asset(mesh)
    component.set_editor_property('override_materials', [])
    for prop, prop_value in [('ammo', magazine), ('mag_capacity', magazine), ('fire_delay', interval),
                             ('automatic', automatic), ('use_scatter', scatter),
                             ('damage', round(ak.get_editor_property('damage') * scale, 1)),
                             ('head_shot_damage', round(ak.get_editor_property('head_shot_damage') * scale, 1)),
                             ('fire_animation', None)]:
        cdo.set_editor_property(prop, prop_value)
    assert lib.save_loaded_asset(bp, only_if_is_dirty=False)

    ammo = duplicate(ITEMS + '/DA_Item_Ammo', ITEMS + '/DA_Item_' + ammo_id)
    for prop, prop_value in {'item_id': ammo_id, 'label': ammo_label, 'description': label + ' 适用。'}.items():
        ammo.set_editor_property(prop, prop_value)
    assert lib.save_loaded_asset(ammo)

    gun = duplicate(ITEMS + '/DA_Item_AK', ITEMS + '/DA_Item_' + ident)
    for prop, prop_value in {'item_id': ident, 'label': label, 'weapon_class': bp.generated_class(),
                             'magazine_capacity': magazine, 'default_ammo': ammo_id,
                             'compatible_ammo': [ammo_id], 'compatible_attachments': [],
                             'size': unreal.IntPoint(*size), 'display_rarity': rarity, 'sell_value': value,
                             'description': description}.items():
        gun.set_editor_property(prop, prop_value)
    assert lib.save_loaded_asset(gun)
    report.append({'id': ident, 'blueprint': bp.get_path_name(), 'damage': cdo.get_editor_property('damage'),
                   'sockets': [str(s) for s in sockets]})
print('EXTRA_WEAPONS', json.dumps(report, ensure_ascii=False))
