"""Shorten the player's standing capsule so ~1.8 m door lintels don't stop a standing player (2026-11-04 #23).

Capsule half height 92 -> 86 (172 cm tall; the head may brush a lintel visually, which is the usual FPS
trade-off). The mesh is raised and BaseEyeHeight increased by the same amount, so the body stays on the
floor and the camera stays at the same world height. Idempotent: does nothing if already at TARGET.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/set_capsule_height.py" -NullRHI -unattended -nosound
"""
import json

import unreal

BP = '/Game/Characters/Stinger/Blueprints/BP_StingerCharacter'
TARGET = 86.0

bp = unreal.load_asset(BP)
cdo = unreal.get_default_object(bp.generated_class())
cap = cdo.get_editor_property('capsule_component')
radius, half = cap.get_unscaled_capsule_radius(), cap.get_unscaled_capsule_half_height()
delta = half - TARGET
mesh = cdo.get_editor_property('mesh')
loc = mesh.get_editor_property('relative_location')
eye = cdo.get_editor_property('base_eye_height')
if abs(delta) > 0.01:
    cap.set_capsule_size(radius, TARGET)
    mesh.set_editor_property('relative_location', unreal.Vector(loc.x, loc.y, loc.z + delta))
    cdo.set_editor_property('base_eye_height', eye + delta)
    # 注：首次运行时网格高度没随编译保存下来，已单独补设为 -80（原 -86 + 6）。
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
cdo = unreal.get_default_object(unreal.load_asset(BP).generated_class())
print('CAPSULE_SET', json.dumps({
    'before': {'half': half, 'mesh_z': loc.z, 'eye': eye},
    'after': {'half': cdo.get_editor_property('capsule_component').get_unscaled_capsule_half_height(),
              'mesh_z': cdo.get_editor_property('mesh').get_editor_property('relative_location').z,
              'eye': cdo.get_editor_property('base_eye_height')}}))
