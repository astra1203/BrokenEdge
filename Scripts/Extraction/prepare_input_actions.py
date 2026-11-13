"""Create the Extraction input assets once; refuse to overwrite existing authored assets."""
import json
import os
from pathlib import Path
import unreal

ROOT = '/Game/Input/Extraction'
KEYS = {
    'Menu': ['Escape'], 'Inventory': ['Tab'], 'TacticalMap': ['M'],
    'UseMedical': ['H'], 'Prone': ['Z'], 'Heal': ['V'], 'Smoke': ['X'],
    'Grenade': ['G'], 'Ping': ['MiddleMouseButton'], 'SelectPrimary': ['One'],
    'SelectSecondary': ['Two'], 'SelectKnife': ['Three'], 'Interact': ['E'],
    'InteractWorld': ['F'], 'HoldBreath': ['LeftAlt', 'RightAlt'],
    'RotateItem': ['R'],
}
paths = [ROOT + '/IA_' + name for name in KEYS] + [ROOT + '/IMC_Extraction', ROOT + '/IMC_ExtractionUI']
existing = [path for path in paths if unreal.EditorAssetLibrary.does_asset_exist(path)]
assert not existing, 'Input assets already exist; inspect rather than overwrite: ' + ', '.join(existing)
tools = unreal.AssetToolsHelpers.get_asset_tools()
actions = {}
for name in KEYS:
    action = tools.create_asset('IA_' + name, ROOT, unreal.InputAction, unreal.InputAction_Factory())
    assert action, name
    action.set_editor_property('value_type', unreal.InputActionValueType.BOOLEAN)
    if name == 'HoldBreath':
        action.set_editor_property('consume_input', False)
    if name == 'Heal':
        hold = unreal.InputTriggerHold(outer=action)
        hold.set_editor_property('hold_time_threshold', 0.35)
        tap = unreal.InputTriggerTap(outer=action)
        tap.set_editor_property('tap_release_time_threshold', 0.35)
        action.set_editor_property('triggers', [hold, tap])
    actions[name] = action
contexts = {}
for name in ['IMC_Extraction', 'IMC_ExtractionUI']:
    contexts[name] = tools.create_asset(name, ROOT, unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    assert contexts[name], name
for name, keys in KEYS.items():
    context = contexts['IMC_ExtractionUI' if name == 'RotateItem' else 'IMC_Extraction']
    for key_name in keys:
        key = unreal.Key()
        key.set_editor_property('key_name', key_name)
        context.map_key(actions[name], key)
for asset in [*actions.values(), *contexts.values()]:
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset), asset.get_path_name()
report = {'result': 'passed', 'assets': paths, 'generator': __file__, 'keys': KEYS,
          'boundary': 'New assets only. Existing character mappings are unchanged.'}
(Path(os.environ['EXTRACTION_RUN_DIR']) / 'InputAssets.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('EXTRACTION_INPUT_ASSETS_COMPLETE')
