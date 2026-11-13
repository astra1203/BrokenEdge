"""Export the current character defaults before migrating presentation ownership."""
import json
import os
from pathlib import Path
import unreal

PROPERTIES = [
    'first_person_mesh_asset', 'first_person_anim_class', 'first_person_mesh_transform',
    'first_person_aim_mesh_offset', 'first_person_weapon_source_mesh', 'first_person_weapon_visual_mesh',
    'first_person_weapon_fit', 'first_person_weapon_transform', 'first_person_fire_montage',
    'first_person_reload_montage', 'first_person_swap_montage', 'stand_to_prone_anim', 'prone_idle_anim',
    'prone_to_stand_anim', 'prone_fire_anim', 'prone_reload_anim', 'first_person_head_bone',
    'first_person_hidden_material_slots', 'first_person_camera_offset',
]
FIT_PROPERTIES = ['enabled', 'hip', 'aim', 'source_right_hand', 'source_left_hand',
                  'source_left_magazine_out', 'source_left_magazine_in', 'magazine_grip',
                  'magazine_reference', 'magazine_bone', 'magazine_out', 'magazine_in', 'swap_attach_fraction']


def encode(value):
    if value is None or isinstance(value, (str, bool, int, float)):
        return value
    if isinstance(value, unreal.Object):
        return {'asset': value.get_path_name()}
    if isinstance(value, unreal.Transform):
        return {'translation': encode(value.translation), 'rotation': encode(value.rotation), 'scale': encode(value.scale3d)}
    if isinstance(value, unreal.Vector):
        return [value.x, value.y, value.z]
    if isinstance(value, unreal.Quat):
        return [value.x, value.y, value.z, value.w]
    if isinstance(value, unreal.StingerViewWeaponFit):
        return {p: encode(value.get_editor_property(p)) for p in FIT_PROPERTIES}
    if isinstance(value, (list, tuple, unreal.Array)):
        return [encode(v) for v in value]
    return str(value)


def character_blueprints():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    candidates = registry.get_assets_by_path('/Game', recursive=True)
    for asset in sorted(candidates, key=lambda a: str(a.package_name)):
        name = str(asset.asset_name)
        if not name.startswith('BP_') or 'Character' not in name:
            continue
        path = str(asset.package_name)
        cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
        if cls:
            cdo = unreal.get_default_object(cls)
            if isinstance(cdo, unreal.BrokenEdgeCharacter):
                yield path, cdo


def export_defaults():
    rows = []
    for path, character in character_blueprints():
        mesh = character.get_editor_property('mesh')
        rows.append({'blueprint': path, 'world_mesh': encode(mesh.get_skeletal_mesh_asset()),
                     'world_anim_class': encode(mesh.get_editor_property('anim_class')),
                     'world_transform': encode(mesh.get_relative_transform()),
                     'properties': {p: encode(presentation_for(character).get_editor_property(p)) for p in PROPERTIES}})
    assert rows, 'No character blueprints found'
    out = Path(os.environ['EXTRACTION_RUN_DIR'])
    (out / 'CharacterDefaults.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('CHARACTER_PRESENTATION_AUDIT_COMPLETE count=' + str(len(rows)))


def presentation_for(character):
    asset = character.get_editor_property('character_presentation')
    assert isinstance(asset, unreal.BrokenEdgeCharacterPresentation), 'Migrate character presentation before editing its appearance'
    return asset


def get_character_property(character, name):
    return (presentation_for(character) if name in PROPERTIES else character).get_editor_property(name)


def set_character_property(character, name, value):
    if name not in PROPERTIES:
        character.set_editor_property(name, value)
        return
    asset = character.get_editor_property('character_presentation')
    # New/duplicated authoring blueprints receive their own configuration before edits.
    base = character.get_class().get_name().removesuffix('_C').removeprefix('BP_')
    destination = '/Game/Characters/Presentation/DA_' + base + '_Presentation'
    lib = unreal.EditorAssetLibrary
    if not asset or asset.get_path_name().split('.')[0] != destination:
        assert not lib.does_asset_exist(destination), 'Refusing to overwrite an unbound presentation: ' + destination
        if asset:
            asset = lib.duplicate_asset(asset.get_path_name(), destination)
        else:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property('data_asset_class', unreal.BrokenEdgeCharacterPresentation)
            asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(destination.rsplit('/', 1)[1],
                '/Game/Characters/Presentation', unreal.BrokenEdgeCharacterPresentation, factory)
        assert asset
        character.set_editor_property('character_presentation', asset)
    asset.set_editor_property(name, value)


def save_character_presentation(character):
    asset = presentation_for(character)
    mesh = character.get_editor_property('mesh')
    asset.set_editor_property('world_mesh', mesh.get_skeletal_mesh_asset())
    asset.set_editor_property('world_anim_class', mesh.get_editor_property('anim_class'))
    asset.set_editor_property('world_transform', mesh.get_relative_transform())
    errors = list(asset.validate_presentation())
    assert not errors, '\n'.join(errors)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)


def migrate_defaults():
    lib = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    pending = []
    report = []
    for path, character in character_blueprints():
        existing = character.get_editor_property('character_presentation')
        if existing:
            errors = list(existing.validate_presentation())
            report.append({'blueprint': path, 'asset': existing.get_path_name(), 'errors': errors, 'existing': True})
            continue
        name = 'DA_' + path.rsplit('/', 1)[-1].removeprefix('BP_') + '_Presentation'
        destination = '/Game/Characters/Presentation/' + name
        assert not lib.does_asset_exist(destination), 'Refusing to overwrite unbound presentation: ' + destination
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.BrokenEdgeCharacterPresentation)
        asset = tools.create_asset(name, '/Game/Characters/Presentation', unreal.BrokenEdgeCharacterPresentation, factory)
        assert asset
        mesh = character.get_editor_property('mesh')
        for prop in PROPERTIES:
            asset.set_editor_property(prop, character.get_editor_property(prop))
        asset.set_editor_property('world_mesh', mesh.get_skeletal_mesh_asset())
        asset.set_editor_property('world_anim_class', mesh.get_editor_property('anim_class'))
        asset.set_editor_property('world_transform', mesh.get_relative_transform())
        errors = list(asset.validate_presentation())
        report.append({'blueprint': path, 'asset': asset.get_path_name(), 'errors': errors, 'existing': False})
        pending.append((path, character, asset))
    out = Path(os.environ['EXTRACTION_RUN_DIR'])
    (out / 'PresentationMigration.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    assert not any(r['errors'] for r in report), 'Presentation validation failed; see PresentationMigration.json'
    for path, character, asset in pending:
        assert lib.save_loaded_asset(asset)
        character.set_editor_property('character_presentation', asset)
        bp = unreal.load_asset(path)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        assert lib.save_loaded_asset(bp, only_if_is_dirty=False)
    unreal.log('CHARACTER_PRESENTATION_MIGRATION_COMPLETE count=' + str(len(report)))


def validate_defaults():
    lib = unreal.EditorAssetLibrary
    report = []
    for path, character in character_blueprints():
        asset = presentation_for(character)
        errors = list(asset.validate_presentation())
        assert not errors, path + ': ' + '\n'.join(errors)
        bp = unreal.load_asset(path)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        status = str(bp.get_editor_property('status'))
        assert 'ERROR' not in status.upper(), path + ': ' + status
        assert lib.save_loaded_asset(bp, only_if_is_dirty=False)
        report.append({'blueprint': path, 'asset': asset.get_path_name(), 'status': status, 'errors': errors})
    out = Path(os.environ['EXTRACTION_RUN_DIR'])
    (out / 'PresentationValidation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    export_defaults()
    unreal.log('CHARACTER_PRESENTATION_VALIDATION_COMPLETE count=' + str(len(report)))


if __name__ == '__main__':
    if os.environ.get('CHARACTER_PRESENTATION_MODE') == 'presentation-migrate':
        migrate_defaults()
    elif os.environ.get('CHARACTER_PRESENTATION_MODE') == 'presentation-validate':
        validate_defaults()
    else:
        export_defaults()
