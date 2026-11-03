"""Align the M4A1 third-person hand sockets with the play-tested SCAR-H calibration.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/patch_m4a1_hold.py" -NullRHI -unattended -nosound

Both rifles share the same root orientation (muzzle along +Y). The M4A1 3P mesh carried hand-socket rotations
about 25 deg (Hand_R yaw) and 30 deg (LeftHandSocket roll) away from the SCAR-H values that were tuned in play,
which twisted the third-person hold. Rotations (and the right-hand grip offset) are copied from SCAR-H; the
left-hand position stays on the M4A1's own handguard. Idempotent.
"""
import unreal

lib = unreal.EditorAssetLibrary


def sockets(name):
    bp = unreal.load_asset('/Game/Extraction/Weapons/%s/BP_%s' % (name, name))
    mesh = unreal.get_default_object(bp.generated_class()).get_editor_property('weapon_mesh').get_skeletal_mesh_asset()
    return mesh, {str(mesh.get_socket_by_index(i).get_editor_property('socket_name')): mesh.get_socket_by_index(i)
                  for i in range(mesh.num_sockets())}


reference_mesh, reference = sockets('SCARH')
mesh, target = sockets('M4A1')
for name in ('Hand_R', 'LeftHandSocket'):
    assert name in reference and name in target, name
    target[name].set_editor_property('relative_rotation', reference[name].get_editor_property('relative_rotation'))
target['Hand_R'].set_editor_property('relative_location', reference['Hand_R'].get_editor_property('relative_location'))
mesh.modify()
assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)
for name in ('Hand_R', 'LeftHandSocket'):
    print('M4A1_HOLD %s loc=%s rot=%s' % (name, target[name].get_editor_property('relative_location'),
                                          target[name].get_editor_property('relative_rotation')))
