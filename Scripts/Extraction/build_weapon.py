"""Editor stage of the generic weapon pipeline (run after ``prepare_weapon.py``).

    set BROKENEDGE_WEAPON=M4A1
    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/build_weapon.py" -NullRHI -unattended -nosound

Imports the assembled 1P/3P meshes and the listed animations, adds the gameplay
sockets (MuzzleFlash / Hand_R / LeftHandSocket) from the official part sockets,
builds the presentation asset, duplicates the AK blueprint and item definition, and
creates the ammunition definition. Hip/aim fit is the play-tested SCAR-H presentation
fit plus the official per-weapon difference recorded in the manifest. Evidence: Saved/Extraction/Weapons/<id>/Build.json.
"""
import json
import os
import sys
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir())
sys.path.insert(0, str(ROOT / 'Scripts/Extraction'))
sys.path.insert(0, str(ROOT / 'Scripts/Stinger'))
import weapon_specs  # noqa: E402
import repair_import as decoder  # noqa: E402

WEAPON = os.environ.get('BROKENEDGE_WEAPON') or (sys.argv[1] if len(sys.argv) > 1 else '')
assert WEAPON in weapon_specs.WEAPONS, 'Set BROKENEDGE_WEAPON to one of %s' % sorted(weapon_specs.WEAPONS)
SPEC = weapon_specs.WEAPONS[WEAPON]
SOURCE = weapon_specs.output_dir(ROOT, WEAPON)
MANIFEST = json.loads((SOURCE / 'Manifest.json').read_text(encoding='utf-8'))
FOLDER = '/Game/Extraction/Weapons/' + WEAPON
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
report = {'weapon': WEAPON, 'parts': MANIFEST['parts'], 'status': 'imported_fit_not_playtested'}


def import_assembly(view):
    folder = FOLDER + '/Assembly' + view
    task = unreal.AssetImportTask()
    task.filename = str(SOURCE / MANIFEST['assemblies'][view])
    task.destination_path = folder
    task.automated = True
    task.save = True
    task.replace_existing = True
    tools.import_asset_tasks([task])
    meshes = [unreal.load_asset(p) for p in lib.list_assets(folder, recursive=True) if '/SkeletalMeshes/' in p]
    meshes = [m for m in meshes if isinstance(m, unreal.SkeletalMesh)]
    assert len(meshes) == 1, [m.get_path_name() for m in meshes]
    return meshes[0]


def import_animations():
    decoder.SOURCE = SOURCE
    decoder.DEST = FOLDER
    result = {}
    for role, member in MANIFEST['animations'].items():
        imported = decoder.import_file(SOURCE / member)
        assert imported['coverage'] >= .9, (role, imported['coverage'])
        mapped = [s for s in imported['sequences'] if 'Mapped' in s['assets']]
        assert mapped, role
        result[role] = unreal.load_asset(mapped[0]['assets']['Mapped'])
        report.setdefault('animations', {})[role] = {'source': member, 'coverage': imported['coverage'],
                                                    'asset': mapped[0]['assets']['Mapped']}
    return result


sockets = json.loads((SOURCE / 'AssemblySockets.json').read_text(encoding='utf-8'))
parts = {p['model'][:p['model'].index('_')]: p['model'] for p in MANIFEST['parts']}
receiver = MANIFEST['parts'][0]['model']


def position(model, view, socket):
    row = sockets[model + '_' + view]
    p = row['SocketLocation'][row['SocketNames'].index(socket)]
    return unreal.Vector(p['X'], p['Y'], p['Z'])


anims = import_animations()
pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anims['idle'], 0, unreal.AnimPoseEvaluationOptions())
right = unreal.AnimPoseExtensions.get_bone_pose(pose, 'RightHand', unreal.AnimPoseSpaces.WORLD)
left = unreal.AnimPoseExtensions.get_bone_pose(pose, 'LeftHand', unreal.AnimPoseSpaces.WORLD)

meshes = {}
for view in ('1P', '3P'):
    mesh = import_assembly(view)
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    bone = comp.get_socket_transform('Root', unreal.RelativeTransformSpace.RTS_COMPONENT)
    muzzle = position(receiver, view, 'Bar_point') + position(parts['Bar'], view, 'MuzzleFlashSocket')
    support = position(receiver, view, 'Han_point') + position(parts['Han'], view, 'HandIKPoint')
    grip = position(receiver, view, 'PisG_point') + unreal.Vector(0, -2, -3)
    for name, point, rotation in [('MuzzleFlash', muzzle, unreal.Rotator(yaw=90).quaternion()),
                                  ('Hand_R', grip, right.rotation), ('LeftHandSocket', support, left.rotation)]:
        offset = unreal.Transform(location=bone.rotation.inversed().rotate_vector(point - bone.translation),
                                  rotation=(bone.rotation.inversed() * rotation).rotator())
        unreal.StingerAssetLibrary.add_mesh_socket(mesh, name, 'Root', offset)
    assert lib.save_loaded_asset(mesh)
    meshes[view] = mesh
    report.setdefault('meshes', {})[view] = mesh.get_path_name()


def duplicate(source, target):
    return unreal.load_asset(target) if lib.does_asset_exist(target) else lib.duplicate_asset(source, target)


profile_class = unreal.load_class(None, '/Script/BrokenEdge.WeaponPresentation')
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class', profile_class)
profile_path = FOLDER + '/DA_' + WEAPON + '_Presentation'
profile = unreal.load_asset(profile_path) or tools.create_asset(profile_path.rsplit('/', 1)[-1], FOLDER,
                                                               profile_class, factory)
fit = unreal.StingerViewWeaponFit()
fit.set_editor_property('enabled', True)
reference = unreal.load_asset(weapon_specs.REFERENCE_FIT['presentation']).get_editor_property('view_fit')
for key in ('hip', 'aim'):
    base = reference.get_editor_property(key)
    location = base.translation + unreal.Vector(*MANIFEST['view_fit'][key])
    fit.set_editor_property(key, unreal.Transform(location=location, rotation=base.rotation.rotator()))
    report.setdefault('view_fit', {})[key] = [location.x, location.y, location.z]
report['view_fit']['official_delta'] = MANIFEST['view_fit']
fit.set_editor_property('source_right_hand', right)
fit.set_editor_property('source_left_hand', left)
fit.set_editor_property('magazine_bone', 'Mag_point')
view_comp = unreal.SkeletalMeshComponent()
view_comp.set_skeletal_mesh_asset(meshes['1P'])
fit.set_editor_property('magazine_reference',
                        view_comp.get_socket_transform('Mag_point', unreal.RelativeTransformSpace.RTS_COMPONENT))
reload = anims['reload']
for prop, fraction in [('source_left_magazine_out', .12), ('source_left_magazine_in', .52)]:
    p = unreal.AnimPoseExtensions.get_anim_pose_at_time(reload, reload.get_play_length() * fraction,
                                                        unreal.AnimPoseEvaluationOptions())
    fit.set_editor_property(prop, unreal.AnimPoseExtensions.get_bone_pose(p, 'LeftHand', unreal.AnimPoseSpaces.WORLD))
fit.set_editor_property('magazine_grip', unreal.Transform(
    location=fit.magazine_reference.translation + unreal.Vector(-3, 0, -8),
    rotation=fit.source_left_magazine_out.rotation.rotator()))
profile.set_editor_property('view_fit', fit)
profile.set_editor_property('view_mesh', meshes['1P'])
profile.set_editor_property('idle', anims['idle'])
profile.set_editor_property('aim', anims['aim'])
movement = unreal.load_asset('/Game/Characters/Stinger/Animations/DeltaForceLocomotion/DA_RifleLocomotion')
assert movement, 'Configure the original rifle locomotion set first'
profile.set_editor_property('locomotion', movement)
for prop, role in [('view_fire', 'fire'), ('view_reload', 'reload'), ('view_swap', 'swap')]:
    montage = unreal.StingerAssetLibrary.create_visual_montage(anims[role], FOLDER + '/Montages/AM_' + role)
    assert montage and lib.save_loaded_asset(montage)
    profile.set_editor_property(prop, montage)
assert lib.save_loaded_asset(profile)

ak_bp = unreal.load_asset('/Game/Blueprints/Weapon/BP_AK47')
ak = unreal.get_default_object(ak_bp.generated_class())
# 官方默认模式点伤害 M4A1 31 / AKM 40；项目伤害是自定绝对值，这里沿用 AK 的项目值并保持官方比例。
scale = SPEC.get('damage_vs_ak', 1.0)
bp = duplicate('/Game/Blueprints/Weapon/BP_AK47', FOLDER + '/BP_' + WEAPON)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cdo = unreal.get_default_object(bp.generated_class())
cdo.set_editor_property('presentation', profile)
component = cdo.get_editor_property('weapon_mesh')
component.set_skeletal_mesh_asset(meshes['3P'])
component.set_editor_property('override_materials', [])
for prop, value in [('ammo', SPEC['magazine']), ('mag_capacity', SPEC['magazine']),
                    ('fire_delay', SPEC['fire_interval']),
                    ('damage', round(ak.get_editor_property('damage') * scale, 1)),
                    ('head_shot_damage', round(ak.get_editor_property('head_shot_damage') * scale, 1)),
                    ('fire_animation', None)]:
    cdo.set_editor_property(prop, value)
assert lib.save_loaded_asset(bp, only_if_is_dirty=False)
report['blueprint'] = {'path': bp.get_path_name(), 'damage': cdo.get_editor_property('damage'),
                       'head_shot_damage': cdo.get_editor_property('head_shot_damage')}

gun = duplicate('/Game/Extraction/Items/DA_Item_AK', '/Game/Extraction/Items/DA_Item_' + WEAPON)
for prop, value in {'item_id': WEAPON, 'label': SPEC['label'], 'weapon_class': bp.generated_class(),
                    'magazine_capacity': SPEC['magazine'], 'default_ammo': SPEC['ammo'],
                    'compatible_ammo': [SPEC['ammo']],
                    # 附件挂点尚未按这把枪校准，先不开放改装，避免配件浮在空中。
                    'compatible_attachments': [], 'description': SPEC['description']}.items():
    gun.set_editor_property(prop, value)
assert lib.save_loaded_asset(gun)
ammo = duplicate('/Game/Extraction/Items/DA_Item_Ammo', '/Game/Extraction/Items/DA_Item_' + SPEC['ammo'])
for prop, value in {'item_id': SPEC['ammo'], 'label': SPEC['ammo_label'],
                    'description': SPEC['label'] + ' 适用。'}.items():
    ammo.set_editor_property(prop, value)
assert lib.save_loaded_asset(ammo)
(SOURCE / 'Build.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('WEAPON_BUILT', json.dumps(report, ensure_ascii=False))
