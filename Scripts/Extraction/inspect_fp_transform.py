import sys as _presentation_sys
from pathlib import Path as _PresentationPath
_presentation_sys.path.insert(0, str(_PresentationPath(__file__).resolve().parents[1] / "Stinger"))
from character_presentation import get_character_property
from pathlib import Path
import unreal
root = Path(unreal.Paths.project_dir())
bp = unreal.load_asset('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter')
gen = unreal.get_default_object(bp.generated_class())
t = get_character_property(gen, 'first_person_mesh_transform')
melee = get_character_property(gen, 'melee')
out = root / 'Saved/Extraction/FirstPersonTransform.txt'
out.write_text('loc=%s rot=%s scale=%s\nknife_mesh=%s\n' % (
    t.translation, t.rotation, t.scale3d,
    melee.get_editor_property('mesh').get_path_name()), encoding='utf-8')
unreal.log_warning('FP_TRANSFORM_DUMPED')
