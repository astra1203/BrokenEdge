"""Author explicit project tuning on the already validated AK grip; no source formula claim."""
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
csv = root / 'Saved/Extraction/GunsmithAuthoring/GripRecoil.csv'
csv.parent.mkdir(parents=True, exist_ok=True)
csv.write_text('0,1.05\n50,1.0\n100,0.95\n', encoding='utf-8')
factory = unreal.CSVImportFactory()
settings = unreal.CSVImportSettings()
curve_names = [name for name in dir(unreal.CSVImportType) if name.endswith('CURVE_FLOAT')]
linear_names = [name for name in dir(unreal.RichCurveInterpMode) if name.endswith('LINEAR')]
assert len(curve_names) == len(linear_names) == 1, (dir(unreal.CSVImportType), dir(unreal.RichCurveInterpMode))
settings.set_editor_property('import_type', getattr(unreal.CSVImportType, curve_names[0]))
settings.set_editor_property('import_curve_interp_mode', getattr(unreal.RichCurveInterpMode, linear_names[0]))
factory.set_editor_property('automated_import_settings', settings)
task = unreal.AssetImportTask()
task.filename = str(csv)
task.destination_path = '/Game/Extraction/Equipment/Compatibility'
task.destination_name = 'GripRecoil'
task.factory = factory
task.automated = True
task.replace_existing = True
task.save = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
curve = unreal.load_asset('/Game/Extraction/Equipment/Compatibility/GripRecoil')
assert isinstance(curve, unreal.CurveFloat)
assert abs(curve.get_float_value(50) - 1) < .001
grip = unreal.load_asset('/Game/Extraction/Items/DA_Item_VerticalGrip')
tune = unreal.WeaponPartTune()
for key, value in dict(id='后座调校', minimum=0, maximum=100, default=50, step=10,
                       attribute='RecoilScale', operation=unreal.WeaponModifierOperation.MULTIPLY,
                       curve=curve).items():
    tune.set_editor_property(key, value)
grip.set_editor_property('tunes', [tune])
assert unreal.EditorAssetLibrary.save_loaded_asset(grip)
unreal.log('EXTRACTION_GUNSMITH_PREPARED')
