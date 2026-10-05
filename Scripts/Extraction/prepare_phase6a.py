"""Prepare Phase 6A definitions, original rigid attachments, and two supply crates.

Run with the rebuilt UE 5.8.3 editor in isolation. Imported geometry is the
original GLB bind mesh; its source ZIPs and prior evidence are never modified.
"""
import json
import shutil
import sys
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(root / 'Scripts/Extraction'))
from optic_calibration import MOUNT as RED_DOT_MOUNT, settings as optic_settings
sources = root / 'Saved/Extraction/Phase6ASources'
manifest = json.loads((sources / 'Manifest.json').read_text(encoding='utf-8'))
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
destination = '/Game/Extraction/Equipment'
backup = root / 'Saved/Extraction/Phase6ABackup'
backup.mkdir(parents=True, exist_ok=True)
for relative in ['Extraction/Items/DA_Item_Ammo.uasset', 'Maps/SpaceCenter/L_SpaceCenterExtraction.umap']:
    original = root / 'Content' / relative
    target = backup / relative
    if original.exists() and not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)

meshes = {}
for key, source in manifest['rigid_models'].items():
    path = destination + '/' + key
    found = [unreal.load_asset(p) for p in lib.list_assets(path, recursive=True)]
    found = [p for p in found if isinstance(p, unreal.StaticMesh)]
    if not found:
        task = unreal.AssetImportTask()
        task.filename = str(sources / source)
        task.destination_path = path
        task.automated = True
        task.save = True
        task.replace_existing = False
        tools.import_asset_tasks([task])
        found = [unreal.load_asset(p) for p in lib.list_assets(path, recursive=True)]
        found = [p for p in found if isinstance(p, unreal.StaticMesh)]
    assert len(found) == 1, (key, found)
    meshes[key] = found[0]

# Reconstruct the exported opaque glass slots as actual translucent lens materials.
# The reticle is shaded on the original lens geometry and is clipped by its frame.
# A camera-space HUD dot would not verify the imported sight or its ADS alignment.
edit = unreal.MaterialEditingLibrary

def material(name, lens=False, reticle=False):
    path = destination + '/Materials/' + name
    asset = unreal.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, destination + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    # Reuse authored nodes on repeat runs. Deleting expressions loaded/rooted by
    # a commandlet trips UObject::MarkAsGarbage; deterministic positions also
    # keep the material graph stable across calibration runs.
    existing = list(edit.get_material_expressions(asset))

    def node(cls, x, y, **props):
        matches = [n for n in existing if isinstance(n, cls)
                   and tuple(edit.get_material_expression_node_position(n)) == (x, y)]
        assert len(matches) <= 1, (name, cls, x, y)
        value = matches[0] if matches else edit.create_material_expression(asset, cls, x, y)
        for key, content in props.items():
            value.set_editor_property(key, content)
        return value

    tint = node(unreal.MaterialExpressionConstant3Vector, -150, -100,
                constant=unreal.LinearColor(.025, .032, .038, 1))
    if lens:
        asset.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
        asset.set_editor_property('two_sided', True)
        asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
        opacity = node(unreal.MaterialExpressionConstant, -100, 200, r=.035)
        if reticle:
            position = node(unreal.MaterialExpressionWorldPosition, -1000, 0)
            camera = node(unreal.MaterialExpressionCameraPositionWS, -1000, 200)
            local = []
            for index, source in enumerate((position, camera)):
                transform = node(unreal.MaterialExpressionTransformPosition, -800, index * 200,
                                 transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                                 transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
                assert edit.connect_material_expressions(source, '', transform, '')
                local.append(transform)
            inputs = []
            for input_name in ['P', 'Eye']:
                parameter = unreal.CustomInput()
                parameter.set_editor_property('input_name', input_name)
                inputs.append(parameter)
            dot = node(unreal.MaterialExpressionCustom, -550, 0, inputs=inputs,
                       output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                       code='float3 ray=P-Eye; float angular=length(ray.xz)/max(abs(ray.y),0.001); return 1-smoothstep(0.0010,0.0018,angular);')
            edit.connect_material_expressions(local[0], '', dot, 'P')
            edit.connect_material_expressions(local[1], '', dot, 'Eye')
            red = node(unreal.MaterialExpressionConstant3Vector, -550, -200, constant=unreal.LinearColor(12, .015, .005, 1))
            emission = node(unreal.MaterialExpressionMultiply, -300, -100)
            edit.connect_material_expressions(dot, '', emission, 'A')
            edit.connect_material_expressions(red, '', emission, 'B')
            tint = emission
            total = node(unreal.MaterialExpressionAdd, -100, 100)
            edit.connect_material_expressions(opacity, '', total, 'A')
            edit.connect_material_expressions(dot, '', total, 'B')
            opacity = total
        edit.connect_material_property(tint, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        edit.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    else:
        edit.connect_material_property(tint, '', unreal.MaterialProperty.MP_BASE_COLOR)
        roughness = node(unreal.MaterialExpressionConstant, -100, 100, r=.65)
        edit.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    errors = edit.recompile_material(asset)
    assert not errors, (name, list(errors))
    assert lib.save_loaded_asset(asset)
    return asset

glass, reticle = material('M_OpticGlass', lens=True), material('M_OpticReticle', lens=True, reticle=True)
optic = meshes['RedDot_1P']
glass_count = 0
for index, slot in enumerate(optic.get_editor_property('static_materials')):
    slot_name = str(slot.get_editor_property('material_slot_name')).lower()
    if 'glass' in slot_name:
        optic.set_material(index, reticle if 'stained' not in slot_name else glass)
        glass_count += 1
assert glass_count == 2, 'Original 1P optic lens sections were not imported'
assert lib.save_loaded_asset(optic)
adapter_path = destination + '/SM_AKAdapter'
adapter = unreal.load_asset(adapter_path) if lib.does_asset_exist(adapter_path) else lib.duplicate_asset('/Engine/BasicShapes/Cube', adapter_path)
adapter.set_material(0, material('M_AKAdapter'))
assert lib.save_loaded_asset(adapter)

definition_class = unreal.load_class(None, '/Script/BrokenEdge.ExtractionItemDefinition')
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class', definition_class)
medical_mesh = unreal.load_asset('/Game/sA_PickupSet_1/Models/SM_Pickup_Health')
ammo_base = unreal.load_asset('/Game/Extraction/Items/DA_Item_Ammo')
ammo_base.set_editor_property('label', '普通弹')
ammo_base.set_editor_property('penetration', 1)
ammo_base.set_editor_property('damage_scale', 1.0)
assert lib.save_loaded_asset(ammo_base)
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
definitions = [
    ('AmmoAP', '穿甲弹', unreal.ExtractionItemKind.AMMO, (1, 1), 60,
     {'penetration': 3, 'damage_scale': .85, 'world_mesh': ammo_base.get_editor_property('world_mesh')}),
    ('Helmet2', '二级头盔', unreal.ExtractionItemKind.ARMOR, (2, 2), 1,
     {'armor_slot': 2, 'armor_level': 2, 'max_durability': 60.0, 'world_mesh': cube}),
    ('Helmet3', '三级头盔', unreal.ExtractionItemKind.ARMOR, (2, 2), 1,
     {'armor_slot': 2, 'armor_level': 3, 'max_durability': 80.0, 'world_mesh': cube}),
    ('Vest2', '二级胸甲', unreal.ExtractionItemKind.ARMOR, (3, 3), 1,
     {'armor_slot': 3, 'armor_level': 2, 'max_durability': 80.0, 'world_mesh': cube}),
    ('Vest3', '三级胸甲', unreal.ExtractionItemKind.ARMOR, (3, 3), 1,
     {'armor_slot': 3, 'armor_level': 3, 'max_durability': 100.0, 'world_mesh': cube}),
    ('Bandage', '绷带', unreal.ExtractionItemKind.MEDICAL, (1, 1), 1,
     {'use_seconds': 3.0, 'heal_amount': 0.0, 'medical_effect': unreal.ExtractionMedicalEffect.BANDAGE, 'world_mesh': medical_mesh}),
    ('Splint', '夹板', unreal.ExtractionItemKind.MEDICAL, (1, 1), 1,
     {'use_seconds': 5.0, 'heal_amount': 0.0, 'medical_effect': unreal.ExtractionMedicalEffect.SPLINT, 'world_mesh': medical_mesh}),
]
mounts = {'RedDot': RED_DOT_MOUNT, 'VerticalGrip': (0, 34, 5.5), 'Compensator': (0, 62, 8.2)}
# Imported attachment meshes use +Y forward and +Z up, matching the AK mesh.
# Counter the existing iron-sight pitch only on the owner optic, then place its
# actual lens center on the camera's forward ray instead of drawing a HUD dot.
character_bp = lib.load_blueprint_class('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired')
character_defaults = unreal.get_default_object(character_bp)
aim_fit = character_defaults.get_editor_property('first_person_weapon_fit').get_editor_property('aim')
optic_config = optic_settings(aim_fit)
optic_view_mount = optic_config['first_person_mount']
optic_offset = optic_config['optic_aim_offset']
for ident, label, slot, scatter in [
    ('RedDot', '原作红点瞄具', unreal.ExtractionAttachmentSlot.OPTIC, 1.0),
    ('VerticalGrip', '原作垂直握把', unreal.ExtractionAttachmentSlot.GRIP, .8),
    ('Compensator', '原作枪口补偿器', unreal.ExtractionAttachmentSlot.MUZZLE, .85),
]:
    mount = unreal.Transform(location=unreal.Vector(*mounts[ident]))
    definitions.append((ident, label, unreal.ExtractionItemKind.ATTACHMENT, (1, 1), 1,
                        {'attachment_slot': slot, 'scatter_scale': scatter,
                         'world_mesh': meshes[ident + '_3P'], 'first_person_mesh': meshes[ident + '_1P'],
                         'mount': mount, 'first_person_mount': optic_view_mount if ident == 'RedDot' else mount,
                         'adapter_mesh': adapter if ident != 'Compensator' else None,
                         'adapter_mount': optic_config['adapter_mount'] if ident == 'RedDot' else unreal.Transform(
                             location=unreal.Vector(0, 34, 6), scale=unreal.Vector(.026, .09, .012)),
                         # Target is the wrist: the closed palm center sits about 9.5 cm forward.
                         'support_grip': unreal.Transform(location=unreal.Vector(2, 24.5, -.3), rotation=unreal.Rotator(yaw=90)),
                         'optic_aim_offset': optic_offset if ident == 'RedDot' else unreal.Vector()}))
for ident, label, kind, size, stack, extra in definitions:
    name = 'DA_Item_' + ident
    path = '/Game/Extraction/Items/' + name
    asset = unreal.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, '/Game/Extraction/Items', definition_class, factory)
    assert asset
    values = {'item_id': ident, 'label': label, 'kind': kind, 'size': unreal.IntPoint(*size), 'max_stack': stack}
    values.update(extra)
    for key, value in values.items():
        asset.set_editor_property(key, value)
    assert lib.save_loaded_asset(asset)

assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
for actor in actors.get_all_level_actors():
    if 'ExtractionPhase6A' in [str(t) for t in actor.tags]:
        actors.destroy_actor(actor)
loot_class = unreal.load_class(None, '/Script/BrokenEdge.ExtractionLootActor')
for tier, position in [(2, (1490, 4660, 425)), (3, (-1140, -420, 425))]:
    crate = actors.spawn_actor_from_class(loot_class, unreal.Vector(*position))
    crate.set_actor_label(f'ExtractionEquipmentSupply{tier}')
    crate.set_editor_property('tags', ['ExtractionPhase6A'])
    crate.set_editor_property('equipment_tier', tier)
    crate.set_editor_property('label', f'装备补给箱 · {tier}级')
assert levels.save_current_level()
report = {'result': 'prepared', 'definitions': [r[0] for r in definitions],
          'mounts': mounts, 'optic_view_mount': str(optic_view_mount), 'optic_aim_offset': str(optic_offset),
          'meshes': {k: {'asset': v.get_path_name(), 'bounds': str(v.get_bounding_box())} for k, v in meshes.items()},
          'sources': manifest,
          'visual_status': 'Mounts require runtime visual acceptance; no acceptance implied by import.'}
(root / 'Saved/Extraction/Phase6AAssets.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
unreal.log('EXTRACTION_PHASE6A_ASSETS_PREPARED')
