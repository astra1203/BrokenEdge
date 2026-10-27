"""Preserve detailed stair collision in isolated Phase7E native mesh copies."""
import unreal
from restoration_common import PROFILE


SOURCES = [
    '/Game/Environment/Structures/HAAVK/Building/FutureTech_Ma1/SM_H_Building_FutureTech_Bridge_01P',
    '/Game/Environment/Structures/IRIS/RoadFacility/Ground/SpaceCenter/SM_I_RoadFacility_Ground_L_13',
]


def repair(g):
    assert PROFILE['phase'] in {'7E','7F'}
    lib,state=g['LIB'],g['state']
    rows=[]
    for source in SOURCES:
        key='mesh:'+source
        original=state[key]['asset']
        mesh=lib.load_asset(original)
        settings=mesh.get_editor_property('nanite_settings')
        before={'fallback_target':str(settings.fallback_target),
                'fallback_percent_triangles':settings.fallback_percent_triangles,
                'fallback_relative_error':settings.fallback_relative_error}
        target=g['DEST']+'/Collision7E/'+g['identity'](source)+'/'+source.rsplit('/',1)[-1]
        if original.split('.')[0]!=target:
            assert not lib.does_asset_exist(target),'Unowned collision asset already exists: '+target
            mesh=lib.duplicate_asset(original,target)
            assert mesh,target
        settings=mesh.get_editor_property('nanite_settings')
        settings.set_editor_property('fallback_target',unreal.NaniteFallbackTarget.PERCENT_TRIANGLES)
        settings.set_editor_property('fallback_percent_triangles',1.)
        settings.set_editor_property('fallback_relative_error',0.)
        mesh.set_editor_property('nanite_settings',settings)
        assert lib.save_loaded_asset(mesh)
        state[key]=dict(state[key],asset=mesh.get_path_name(),collision_fidelity='full_triangles_7e')
        g['write_json'](g['STATE_FILE'],state)
        rows.append({'source':source,'original_asset':original,'asset':mesh.get_path_name(),'before':before,
                     'full_collision_fallback':True})
    g['report']['collision_fidelity']=rows
