"""Check native transforms, imported geometry bounds, texture roles and collision settings."""
import json
import math
import struct
import itertools
import unreal
from restoration_world import PREVIEW,geometry_signature,material_bindings


def audit(g):
    manifest,state,report=g['MANIFEST'],g['state'],g['report'];lib=g['LIB']
    integrated=g['TASK']=='audit-integrated'
    target='/Game/Maps/SpaceCenter/L_SpaceCenterExtraction' if integrated else PREVIEW
    editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);assert editor.load_level(target)
    if integrated:
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        stream=unreal.GameplayStatics.get_streaming_level(world,'/Game/Maps/SpaceCenter/L_SpaceCenterNorth')
        assert stream and stream.get_loaded_level()
        unreal.EditorLevelUtils.set_level_visibility(stream.get_loaded_level(),True,False)
    pointer=g['read'](g['WORK']/('IntegrationBindings.json' if integrated else 'PreviewBindings.json'))
    assert pointer['geometry_sha256']==geometry_signature(manifest)
    bindings=g['read'](pointer['bindings_file'])
    native_actors=list(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
    if integrated:
        from integrate_restoration import FAKE_GROUND
        assert not any(a.get_actor_label() in FAKE_GROUND for a in native_actors),'Legacy placeholder geometry returned'
    actors={(a.get_level().get_path_name().split('.')[0],a.get_actor_label()):a for a in native_actors}
    assert len(actors)==len(native_actors),'Duplicate native actor labels'
    source={a['id']:a for a in manifest['actors']};errors=[];max_position=0.;max_rotation=0.;max_scale=0.;max_world_geometry=0.
    slots=g['read'](g['WORK']/'SlotBindings.json')
    legacy=g['read'](g['ROOT']/'Scripts/SpaceCenter/overrides.json')
    manual=g['read'](g['ROOT']/'Scripts/SpaceCenter/restoration_overrides.json')
    report['override_sha256']={name:g['sha'](g['ROOT']/'Scripts/SpaceCenter'/name)
                               for name in ['overrides.json','restoration_overrides.json']}
    seen=set()
    for binding in bindings:
        ident=binding['id'];assert ident not in seen;seen.add(ident)
        original=source[ident]
        if ident in set(legacy['preserve_actors'])|set(manual['preserve_actors']):continue
        desired=manual['actor_transforms'].get(ident,original['transform']);actor=actors[(binding.get('map',target),binding['actor'])]
        if binding['instance'] is None:
            actual=actor.get_actor_transform();component=actor.static_mesh_component
        else:
            component=actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
            actual=component.get_instance_transform(binding['instance'],True)
            if isinstance(actual,tuple):actual=next(v for v in actual if isinstance(v,unreal.Transform))
            if not isinstance(actual,unreal.Transform):raise RuntimeError('Missing native instance transform: '+repr({'binding':binding,'count':component.get_instance_count(),'value':str(actual),'component':component.get_path_name()}))
        offset=legacy['translation_offsets_cm'].get(ident,[0,0,0])
        expected=[desired['Translation'][k]-manifest['origin'][i]+offset[i] for i,k in enumerate('XYZ')]
        error=max(abs(getattr(actual.translation,k)-expected[i]) for i,k in enumerate('xyz'))
        max_position=max(max_position,error)
        dot=abs(sum(getattr(actual.rotation,k.lower())*desired['Rotation'][k] for k in 'XYZW'))
        max_rotation=max(max_rotation,math.degrees(2*math.acos(min(1,dot))))
        max_scale=max(max_scale,max(abs(getattr(actual.scale3d,k.lower())-desired['Scale3D'][k]) for k in 'XYZ'))
        if error>1:errors.append({'id':ident,'position_error_cm':error})
        # HISM serializes matrices. A negative uniform scale may decompose into
        # a different quaternion and signed scale with exactly the same geometry.
        # Compare affine transforms over all local bounds corners, not raw Q/S.
        def rotated(point,quat,scale):
            x,y,z=[point[i]*scale[i] for i in range(3)]
            qx,qy,qz,qw=quat;norm=math.sqrt(sum(v*v for v in quat))
            qx,qy,qz,qw=[v/norm for v in quat]
            tx,ty,tz=2*(qy*z-qz*y),2*(qz*x-qx*z),2*(qx*y-qy*x)
            return [x+qw*tx+qy*tz-qz*ty,y+qw*ty+qz*tx-qx*tz,z+qw*tz+qx*ty-qy*tx]
        box=component.static_mesh.get_bounding_box()
        source_quat=[desired['Rotation'][k] for k in 'XYZW'];source_scale=[desired['Scale3D'][k] for k in 'XYZ']
        native_quat=[getattr(actual.rotation,k) for k in 'xyzw'];native_scale=[getattr(actual.scale3d,k) for k in 'xyz']
        geometry_error=0.
        for corner in itertools.product(*[(getattr(box.min,k),getattr(box.max,k)) for k in 'xyz']):
            a=rotated(corner,source_quat,source_scale);b=rotated(corner,native_quat,native_scale)
            geometry_error=max(geometry_error,max(abs(a[i]+expected[i]-b[i]-getattr(actual.translation,k)) for i,k in enumerate('xyz')))
        max_world_geometry=max(max_world_geometry,geometry_error)
        if geometry_error>1:errors.append({'id':ident,'world_geometry_error_cm':geometry_error})
        if component.static_mesh.get_path_name()!=state['mesh:'+original['mesh']]['asset']:
            errors.append({'id':ident,'error':'native_mesh_differs'})
        for i,path in material_bindings(component.static_mesh,original,slots):
            if path in manifest['materials']:
                mat=component.get_material(i)
                expected_material=manual['materials'].get(path,state['restoration_material:'+path]['asset'])
                if not mat or mat.get_path_name().split('.')[0]!=expected_material.split('.')[0]:
                    errors.append({'id':ident,'slot':i,'error':'native_material_differs'})
    if seen!=set(source):errors.append({'error':'missing_or_extra_source_bindings','missing':sorted(set(source)-seen)})
    bounds_errors=[];max_bound_error=0;nanite_enabled=0
    entries=[('mesh',e) for e in manifest['meshes'].values()]+[('terrain',e) for e in manifest['terrain']['tiles']]
    for kind,e in entries:
        obj=lib.load_asset(state[kind+':'+e['source']]['asset'])
        nanite_enabled+=bool(obj.get_editor_property('nanite_settings').enabled)
        box=obj.get_bounding_box();data=g['Path'](e['file']).read_bytes()
        json_length=struct.unpack_from('<I',data,12)[0];doc=json.loads(data[20:20+json_length])
        accessors=[doc['accessors'][p['attributes']['POSITION']] for mesh in doc['meshes'] for p in mesh['primitives']]
        minimum=[min(a['min'][i] for a in accessors)*100 for i in [0,2,1]]
        maximum=[max(a['max'][i] for a in accessors)*100 for i in [0,2,1]]
        delta=max(abs(getattr(box.min,k)-minimum[i]) for i,k in enumerate('xyz'))
        delta=max(delta,max(abs(getattr(box.max,k)-maximum[i]) for i,k in enumerate('xyz')))
        max_bound_error=max(max_bound_error,delta)
        if delta>1:bounds_errors.append({'source':e['source'],'error_cm':delta})
        body=obj.get_editor_property('body_setup')
        if not body or body.get_editor_property('collision_trace_flag')!=unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
            errors.append({'source':e['source'],'error':'missing_complex_collision'})
    texture_errors=[]
    for entry in manifest['texture_bindings'].values():
        obj=lib.load_asset(state['texture:'+entry['source']+':'+entry['role']]['asset'])
        if obj.get_editor_property('srgb')!=(entry['role']=='color'):
            texture_errors.append(entry['source'])
    report.update(map=target,geometry_sha256=geometry_signature(manifest),instances=len(bindings),max_position_error_cm=max_position,max_rotation_error_deg=max_rotation,
                  max_scale_error=max_scale,max_world_geometry_error_cm=max_world_geometry,max_mesh_bound_error_cm=max_bound_error,
                  rotation_scale_note='Raw Q/S can differ after negative-scale matrix decomposition; transformed bounds corners must match within 1cm.',
                  native_actors=len(native_actors),nanite_enabled_meshes=nanite_enabled,
                  geometry_errors=errors,bounds_errors=bounds_errors,texture_role_errors=texture_errors)
    if errors or bounds_errors or texture_errors:raise RuntimeError('Native audit failed; see detailed report')
