"""Report what the SCAR-H weapon actually renders, so mount numbers can be checked
against the real mesh instead of the source-coordinate assumptions.

Run inside the editor:

    py "D:/Users/SCSJ/BrokenEdge2.0/Scripts/Extraction/probe_scar_frame.py"

Prints, for the 3P body mesh and the 1P view mesh:
  * the asset path actually assigned (a stale or different mesh explains a wrong rail height)
  * local-space bounds origin / extent, which reveal the import axis convention and any shift
  * the Root bone transform in component space
and the attachment placement the data currently resolves to for SCARH.
"""
import unreal

BP = '/Game/Extraction/Weapons/SCARH/BP_SCARH'
DA = '/Game/Extraction/Items/DA_Item_RedDot'
WEAPON = 'SCARH'


def describe(label, mesh):
    if not mesh:
        print('%s: <none>' % label)
        return
    b = mesh.get_bounds()
    print('%s: %s' % (label, mesh.get_path_name()))
    print('   bounds origin=(%.2f, %.2f, %.2f) extent=(%.2f, %.2f, %.2f)'
          % (b.origin.x, b.origin.y, b.origin.z,
             b.box_extent.x, b.box_extent.y, b.box_extent.z))
    print('   z range %.2f .. %.2f' % (b.origin.z - b.box_extent.z, b.origin.z + b.box_extent.z))
    if isinstance(mesh, unreal.SkeletalMesh):
        comp = unreal.SkeletalMeshComponent()
        comp.set_skeletal_mesh_asset(mesh)
        root = comp.get_socket_transform('Root', unreal.RelativeTransformSpace.RTS_COMPONENT)
        print('   Root bone @ component: (%.2f, %.2f, %.2f)'
              % (root.translation.x, root.translation.y, root.translation.z))


bp = unreal.load_asset(BP)
if bp:
    cdo = unreal.get_default_object(bp.generated_class())
    describe('3P weapon mesh', cdo.get_editor_property('weapon_mesh').get_skeletal_mesh_asset())
    profile = cdo.get_editor_property('presentation')
    if profile:
        describe('1P view mesh', profile.get_editor_property('view_mesh'))
        fit = profile.get_editor_property('view_fit')
        if fit:
            aim = fit.get_editor_property('aim')
            print('   view fit aim=(%.2f, %.2f, %.2f)' % (aim.translation.x, aim.translation.y, aim.translation.z))
else:
    print('missing blueprint ' + BP)

asset = unreal.load_asset(DA)
if asset:
    for prop in ('world_mesh', 'first_person_mesh'):
        describe('optic ' + prop, asset.get_editor_property(prop))
    for prop in ('mount', 'first_person_mount'):
        t = asset.get_editor_property(prop)
        print('optic %s base=(%.3f, %.3f, %.3f)'
              % (prop, t.translation.x, t.translation.y, t.translation.z))
    for prop in ('mount_by_weapon', 'first_person_mount_by_weapon'):
        table = asset.get_editor_property(prop)
        for key, t in table.items():
            print('optic %s[%s]=(%.3f, %.3f, %.3f)'
                  % (prop, key, t.translation.x, t.translation.y, t.translation.z))

print('PROBE_DONE')
