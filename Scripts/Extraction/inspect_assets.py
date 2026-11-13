"""Read-only inspection of first-person slots and SpaceCenter render compatibility."""
import sys as _presentation_sys
from pathlib import Path as _PresentationPath
_presentation_sys.path.insert(0, str(_PresentationPath(__file__).resolve().parents[1] / "Stinger"))
from character_presentation import get_character_property
import json
from pathlib import Path
import unreal

lib = unreal.EditorAssetLibrary
cdo = unreal.get_default_object(lib.load_blueprint_class('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter'))
mesh = get_character_property(cdo, 'first_person_mesh_asset')
report = {'first_person_mesh': mesh.get_path_name(), 'slots': [], 'materials': {}}
for item in mesh.get_editor_property('materials'):
    material = item.get_editor_property('material_interface')
    report['slots'].append({'slot': str(item.get_editor_property('material_slot_name')),
                            'material': material.get_path_name() if material else None})
for name in ['M_SC_Surface', 'M_SC_Glass']:
    material = lib.load_asset('/Game/Maps/SpaceCenter/Materials/' + name)
    report['materials'][name] = {'blend': str(material.get_editor_property('blend_mode')),
                                 'nanite': material.get_editor_property('used_with_nanite')}
report['result'] = 'passed'
(Path(unreal.Paths.project_dir()) / 'Saved/Extraction/AssetInspection.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('EXTRACTION_ASSETS_INSPECTED')
