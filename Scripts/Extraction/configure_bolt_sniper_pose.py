"""Correct the X-forward sniper's first-person frame; AK uses a Y-forward mesh.

Grip sockets already convert sniper -> AK coordinates. The old presentation
copied AK's camera transform without composing that conversion, rotating the
entire sniper 90 degrees across the screen. Only the sniper presentation changes.
"""
import unreal

lib = unreal.EditorAssetLibrary
source = unreal.load_asset('/Game/Characters/Stinger/Animations/DeltaForceLocomotion/DA_AKLocomotionPresentation')
sniper = unreal.load_asset('/Game/Extraction/Weapons/BoltSniper/DA_BoltSniper_Presentation')
source_fit = source.get_editor_property('view_fit')
fit = sniper.get_editor_property('view_fit')
to_ak = unreal.Transform(unreal.Vector(0, -1, 0), unreal.Rotator(yaw=90), unreal.Vector(1, 1, 1))
for key in ['hip', 'aim']:
    transform = to_ak.multiply(source_fit.get_editor_property(key))
    if key == 'aim':
        p = transform.translation
        transform.translation = unreal.Vector(p.x, p.y, p.z - (21.5 - 13.95))
    fit.set_editor_property(key, transform)
    print('BOLT_SNIPER_FRAME', key, transform)
sniper.set_editor_property('view_fit', fit)
sniper.set_editor_property('scope_overlay', unreal.load_asset('/Game/Blueprints/UI/SniperScopeNoBG'))
sniper.set_editor_property('scope_fov', 30.0)
assert lib.save_loaded_asset(sniper, only_if_is_dirty=False)
print('BOLT_SNIPER_POSE_SAVED')
