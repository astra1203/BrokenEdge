"""Restore the pre-migration bindings without deleting any assets or changing source ZIPs."""
import json
from pathlib import Path
import unreal

work=Path(unreal.Paths.project_saved_dir())/'Stinger/Locomotion'
backup=work/'BindingsBefore.json'
if not backup.exists():
    backup=Path(unreal.Paths.project_dir())/'Artifacts/Stinger/LocomotionEvidence/BindingsBefore.json'
bindings=json.loads(backup.read_text(encoding='utf-8'))
for binding in bindings:
    bp=unreal.load_asset(binding['asset'])
    cdo=unreal.get_default_object(bp.generated_class())
    before=binding['before']
    if binding['property']=='mesh.anim_class':
        cdo.get_editor_property('mesh').set_anim_instance_class(unreal.load_class(None,before))
    elif binding['property']=='presentation':
        cdo.set_editor_property('presentation',unreal.load_asset(before) if before else None)
    else:raise ValueError(binding)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False)
print('LOCOMOTION_BINDINGS_RESTORED',len(bindings))
