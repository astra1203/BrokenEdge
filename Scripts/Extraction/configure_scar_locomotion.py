"""Bind SCAR to the existing original rifle movement set, preserving its weapon-specific presentation."""
import unreal

profile = unreal.load_asset('/Game/Extraction/Weapons/SCARH/DA_SCARH_Presentation')
movement = unreal.load_asset('/Game/Characters/Stinger/Animations/DeltaForceLocomotion/DA_RifleLocomotion')
assert profile and movement
assert movement.get_editor_property('view_sprint') and movement.get_editor_property('view_idle')
profile.set_editor_property('locomotion', movement)
assert unreal.EditorAssetLibrary.save_loaded_asset(profile)
print('SCAR_LOCOMOTION_BOUND', profile.get_path_name(), movement.get_path_name())
