"""Shared AK/RedDot placement, in weapon component centimetres.

OpticAudit02 exported the actual view mesh: receiver roof at Y=9..15 is
Z=12.440467, highest iron-sight vertex is Z=13.772024. The original optic
glass spans local Z=0.580842..2.498598. Its previous base Z=11 intersected
the receiver. A Z=14 base clears the sights; the 1.6 cm riser seats into
the roof by 0.04 cm. Preserve the source optic scale and fore/aft position.
"""
import unreal

MOUNT = (0.0, 12.0, 14.0)
ADAPTER_BOTTOM = 12.4
LENS_CENTER = (0.0000162516, 2.1473472938, 1.5397197800)


def settings(aim):
    rotation = aim.rotation.inversed() * unreal.Rotator(yaw=-90).quaternion()
    first = unreal.Transform(location=unreal.Vector(*MOUNT), rotation=rotation.rotator())
    center = aim.transform_location(first.transform_location(unreal.Vector(*LENS_CENTER)))
    return {
        'mount': unreal.Transform(location=unreal.Vector(*MOUNT)),
        'first_person_mount': first,
        'adapter_mount': unreal.Transform(
            location=unreal.Vector(0, 12, (ADAPTER_BOTTOM + MOUNT[2]) / 2),
            scale=unreal.Vector(.03, .06, (MOUNT[2] - ADAPTER_BOTTOM) / 100)),
        'optic_aim_offset': unreal.Vector(0, -center.y, -center.z),
    }


def describe(values):
    def vector(v): return [v.x, v.y, v.z]
    result = {}
    for key, value in values.items():
        result[key] = ({'translation': vector(value.translation), 'rotation_xyzw':
                        [value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w],
                        'scale': vector(value.scale3d)} if isinstance(value, unreal.Transform) else vector(value))
    return result
