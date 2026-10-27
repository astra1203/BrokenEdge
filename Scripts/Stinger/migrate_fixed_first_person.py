"""Migrate fixed-first-person camera defaults and remove the obsolete view action.
Run after compiling the native character. Only affected assets are saved.
"""
import json
from pathlib import Path
import unreal

lib = unreal.EditorAssetLibrary
out = Path(unreal.Paths.project_saved_dir()) / 'FixedFirstPerson'
out.mkdir(parents=True, exist_ok=True)
action_path = '/Game/Input/Actions/IA_ToggleCameraView'
action = lib.load_asset(action_path) if lib.does_asset_exist(action_path) else None
report = {'blueprints': [], 'contexts': [], 'legacy_keys': []}
for path in ['/Game/Input/IMC_CharacterDefault', '/Game/Characters/Stinger/Blueprints/Input/IMC_Stinger']:
    context = lib.load_asset(path)
    if action:
        defaults = context.get_editor_property('default_key_mappings')
        report['legacy_keys'] += [str(m.key.get_editor_property("key_name")) for m in defaults.get_editor_property('mappings') if m.action == action]
        context.unmap_all_keys_from_action(action)
        overrides = context.get_editor_property('mapping_profile_overrides')
        for key, data in overrides.items():
            data.set_editor_property('mappings', [m for m in data.get_editor_property('mappings') if m.action != action])
            overrides[key] = data
        context.set_editor_property('mapping_profile_overrides', overrides)
        assert lib.save_loaded_asset(context, False)
    report['contexts'].append(path)
paths = ['/Game/Blueprints/Character/BP_BrokenEdgeCharacter', '/Game/Blueprints/Character/BP_BrokenEdgeCharacter1',
         '/Game/Characters/Stinger/Blueprints/BP_StingerCharacter', '/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired']
for path in paths:
    bp = lib.load_asset(path)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    camera = cdo.get_editor_property('follow_camera')
    capsule = cdo.get_editor_property('capsule_component')
    unreal.StingerAssetLibrary.reattach_component(camera, capsule, 'None')
    camera.set_editor_property('use_pawn_control_rotation', True)
    assert camera.get_attach_parent() == capsule, path
    assert not cdo.get_components_by_class(unreal.SpringArmComponent), path
    assert lib.save_loaded_asset(bp, False)
    report['blueprints'].append(path)
if action:
    refs = list(lib.find_package_referencers_for_asset(action_path, True))
    assert not refs, 'Still referenced: ' + str(refs)
    assert lib.delete_asset(action_path)
assert not lib.does_asset_exist(action_path)
(out/'Migration.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('FIXED_FIRST_PERSON_ASSETS PASSED', report)
