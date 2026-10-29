"""Create missing Extraction templates without overwriting designer changes.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file> after an Editor build.
"""
import json
from pathlib import Path
import unreal

paths = ['/Game/UI/Extraction/WBP_ExtractionHUD',
         '/Game/Blueprints/Extraction/BP_ExtractionLootActor']
existed = all(unreal.EditorAssetLibrary.does_asset_exist(path) for path in paths)
result = unreal.ExtractionBlueprintBuilder.create_extraction_blueprints()
assert result == 'PASSED', result
classes = [unreal.EditorAssetLibrary.load_blueprint_class(path) for path in paths]
assert all(classes), 'Missing generated classes'
loot_defaults = unreal.get_default_object(classes[1])
assert loot_defaults.get_editor_property('default_container_mesh'), 'Missing default mesh'
report = {'result': result, 'classes': [c.get_path_name() for c in classes]}
out = Path(unreal.Paths.project_saved_dir()) / 'Extraction/BlueprintRefactor'
out.mkdir(parents=True, exist_ok=True)
(out / 'Assets.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('EXTRACTION_BLUEPRINTS', report)
# Run in a fresh process after first creation so native class defaults can load the assets.
if existed:
    validation = unreal.ExtractionBlueprintBuilder.validate_extraction_blueprints()
    assert validation == 'PASSED', validation
    print('EXTRACTION_BINDINGS', validation)
else:
    print('EXTRACTION_BINDINGS rerun in a fresh editor process to validate native class defaults')
