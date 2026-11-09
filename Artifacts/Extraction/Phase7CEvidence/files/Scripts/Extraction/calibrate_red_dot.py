"""Update only the existing RedDot definition; never recreate maps/supply crates."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(root / 'Scripts/Extraction'))
from optic_calibration import settings, describe

out = Path(os.environ['EXTRACTION_RUN_DIR'])
asset_path = '/Game/Extraction/Items/DA_Item_RedDot'
asset_file = root / 'Content/Extraction/Items/DA_Item_RedDot.uasset'
shutil.copy2(asset_file, out / asset_file.name)
before = hashlib.sha256(asset_file.read_bytes()).hexdigest()
lib = unreal.EditorAssetLibrary
definition = lib.load_asset(asset_path)
assert definition, asset_path
character = unreal.get_default_object(lib.load_blueprint_class('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired'))
aim = character.get_editor_property('first_person_weapon_fit').get_editor_property('aim')
values = settings(aim)
old = describe({key: definition.get_editor_property(key) for key in values})
for key, value in values.items():
    definition.set_editor_property(key, value)
assert lib.save_loaded_asset(definition)
report = {'result': 'prepared', 'asset': asset_path, 'before_sha256': before,
          'after_sha256': hashlib.sha256(asset_file.read_bytes()).hexdigest(),
          'before': old, 'after': describe(values), 'aim': describe({'aim': aim})['aim'],
          'visual_acceptance': 'pending'}
(out / 'Calibration.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('EXTRACTION_OPTIC_PREPARED')
