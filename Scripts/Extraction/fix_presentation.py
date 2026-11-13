"""Narrow, recorded fixes for view-only accessories and imported Nanite materials."""
import sys as _presentation_sys
from pathlib import Path as _PresentationPath
_presentation_sys.path.insert(0, str(_PresentationPath(__file__).resolve().parents[1] / "Stinger"))
from character_presentation import set_character_property, save_character_presentation, presentation_for
import hashlib
import json
from pathlib import Path
import shutil
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved/Extraction/PresentationFix'
out.mkdir(exist_ok=False)
lib = unreal.EditorAssetLibrary
report = {'assets': [], 'glass_components': [], 'result': 'running'}


def backup(asset):
    extension = '.umap' if isinstance(asset, unreal.World) else '.uasset'
    file = root / 'Content' / (asset.get_path_name().split('.')[0].removeprefix('/Game/') + extension)
    copy = out / (asset.get_name() + extension)
    shutil.copy2(file, copy)
    report['assets'].append({'asset': asset.get_path_name(), 'before': hashlib.sha256(file.read_bytes()).hexdigest()})


for name in ['BP_StingerCharacter', 'BP_StingerCharacter_Repaired']:
    blueprint = lib.load_asset('/Game/Characters/Stinger/Blueprints/' + name)
    backup(blueprint)
    cdo = unreal.get_default_object(blueprint.generated_class())
    backup(presentation_for(cdo))
    set_character_property(cdo, 'first_person_hidden_material_slots', ['M_Roy_016_BP_LOD'])
    save_character_presentation(cdo)
    assert lib.save_loaded_asset(blueprint, only_if_is_dirty=False)

surface = lib.load_asset('/Game/Maps/SpaceCenter/Materials/M_SC_Surface')
backup(surface)
surface.set_editor_property('used_with_nanite', True)
unreal.MaterialEditingLibrary.recompile_material(surface)
assert lib.save_loaded_asset(surface, only_if_is_dirty=False)

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
backup(world)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in actors.get_all_level_actors():
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        materials = [component.get_material(i) for i in range(component.get_num_materials())]
        if any(m and m.get_base_material().get_editor_property('blend_mode') == unreal.BlendMode.BLEND_TRANSLUCENT for m in materials):
            component.set_editor_property('disallow_nanite', True)
            report['glass_components'].append(component.get_path_name())
assert levels.save_current_level()
report['result'] = 'passed'
(out / 'Report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('EXTRACTION_PRESENTATION_FIXED', len(report['glass_components']))
