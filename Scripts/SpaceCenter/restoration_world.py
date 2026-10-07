"""Assemble a separately reviewable reconstruction, preserving explicit overrides."""
import collections
import hashlib
import json
import math
import unreal
from restoration_common import PROFILE

PREVIEW=PROFILE.get('preview_map','/Game/Maps/SpaceCenter/L_SpaceCenterRestorationPreview')
VERSION=1


def material_bindings(mesh,source,slots):
    names=[str(m.material_slot_name) for m in mesh.get_editor_property('static_materials')]
    entries=slots[source['mesh']]
    if len(names)!=len(entries):raise RuntimeError('Native material slot count differs: '+source['mesh']+' '+str(names))
    lookup={e['name']:e['source_slot'] for e in entries}
    if set(names)!=set(lookup):raise RuntimeError('Native material slot names differ: '+source['mesh']+' '+str(names)+' '+str(list(lookup)))
    return [(i,source['materials'][lookup[name]]) for i,name in enumerate(names)]


def configure_collision(component,source):
    body=source.get('properties',{}).get('BodyInstance',{})
    no_collision=body.get('CollisionProfileName')=='NoCollision' or body.get('CollisionEnabled')=='ECollisionEnabled::NoCollision'
    component.set_collision_profile_name('NoCollision' if no_collision else 'BlockAll')
    if no_collision:return
    enabled=body.get('CollisionEnabled','').split('::')[-1]
    if enabled=='QueryOnly':component.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    channels={'WorldStatic':unreal.CollisionChannel.ECC_WORLD_STATIC,'WorldDynamic':unreal.CollisionChannel.ECC_WORLD_DYNAMIC,
              'Pawn':unreal.CollisionChannel.ECC_PAWN,'Visibility':unreal.CollisionChannel.ECC_VISIBILITY,
              'Camera':unreal.CollisionChannel.ECC_CAMERA,'PhysicsBody':unreal.CollisionChannel.ECC_PHYSICS_BODY,
              'Vehicle':unreal.CollisionChannel.ECC_VEHICLE,'Destructible':unreal.CollisionChannel.ECC_DESTRUCTIBLE}
    responses={r['Channel']:r['Response'] for r in body.get('CollisionResponses',{}).get('ResponseArray',[])}
    values={'ECR_Ignore':unreal.CollisionResponseType.ECR_IGNORE,'ECR_Overlap':unreal.CollisionResponseType.ECR_OVERLAP,'ECR_Block':unreal.CollisionResponseType.ECR_BLOCK}
    for name,value in responses.items():
        if name in channels:component.set_collision_response_to_channel(channels[name],values[value])
    # This project's hitscan uses Visibility; keep original bullet-blocking props
    # while preserving the source Pawn response (small decorations are not walls).
    if responses.get('ProjectileShootable')=='ECR_Block':
        component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY,unreal.CollisionResponseType.ECR_BLOCK)


def geometry_signature(manifest):
    return hashlib.sha256(json.dumps([[a['id'],a['mesh'],a['transform'],a['materials']] for a in manifest['actors']],sort_keys=True).encode()).hexdigest()


def assemble(g,map_path=PREVIEW,region=None,load=True,save=True):
    lib,tools=g['LIB'],g['TOOLS'];manifest,state,report=g['MANIFEST'],g['state'],g['report']
    editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if load:
        if lib.does_asset_exist(map_path):assert editor.load_level(map_path)
        else:assert editor.new_level(map_path)
    assert editor.set_current_level_by_name(map_path.rsplit('/',1)[-1])
    existing={a.get_actor_label():a for a in subsystem.get_all_level_actors() if a.get_level().get_path_name().startswith(map_path+'.')}
    legacy=g['read'](g['ROOT']/'Scripts/SpaceCenter/overrides.json')
    overrides=g['read'](g['ROOT']/'Scripts/SpaceCenter/restoration_overrides.json')
    slots=g['read'](g['WORK']/'SlotBindings.json')
    report['slot_bindings_sha256']=g['sha'](g['WORK']/'SlotBindings.json')
    preserved=set(legacy['preserve_actors'])|set(overrides['preserve_actors'])
    origin=manifest['origin'];desired=set();groups=collections.defaultdict(list);records=[]

    def spawn(label,cls,location=(0,0,0),rotation=None):
        desired.add(label)
        actor=existing.get(label)
        expected_class=cls.static_class() if isinstance(cls,type) else cls
        if actor and actor.get_class()!=expected_class and 'SpaceCenterRestoration' in [str(t) for t in actor.tags]:
            subsystem.destroy_actor(actor);actor=None
        if not actor:
            actor=subsystem.spawn_actor_from_class(cls,unreal.Vector(*location),rotation or unreal.Rotator())
            if not actor:raise RuntimeError('Failed to create '+label)
            actor.set_actor_label(label)
            actor.set_folder_path('SpaceCenter/Restoration')
            actor.set_editor_property('tags',[unreal.Name('SpaceCenterRestoration')])
        return actor

    def native_mesh(source):return lib.load_asset(state['mesh:'+source]['asset'])
    def native_material(source):
        if not source:return None
        if source in overrides['materials']:
            obj=lib.load_asset(overrides['materials'][source])
            if not obj:raise RuntimeError('Missing explicit material override: '+source)
            return obj
        entry=state.get('restoration_material:'+source)
        if entry:return lib.load_asset(entry['asset'])
        if source.startswith('/Engine/'):return lib.load_asset(source)
        raise RuntimeError('Missing native reconstructed material: '+source)

    def instance_transform(source):
        t=overrides['actor_transforms'].get(source['id'],source['transform']);p,q,s=[t[k] for k in ['Translation','Rotation','Scale3D']]
        location=[p[k]-origin[i] for i,k in enumerate('XYZ')]
        offset=legacy['translation_offsets_cm'].get(source['id'],[0,0,0])
        location=[v+offset[i] for i,v in enumerate(location)]
        # Cooked transform quaternions can be slightly non-unit despite their
        # serialized IsNormalized flag. Normalize explicitly, and avoid the
        # Euler conversion (especially at a 90-degree pitch).
        norm=math.sqrt(sum(q[k]*q[k] for k in 'XYZW'))
        if norm<1e-8:raise RuntimeError('Invalid source quaternion: '+source['id'])
        transform=unreal.Transform(location=unreal.Vector(*location),scale=unreal.Vector(*[s[k] for k in 'XYZ']))
        transform.rotation=unreal.Quat(*[q[k]/norm for k in 'XYZW'])
        return transform

    def collision(source):
        body=source.get('properties',{}).get('BodyInstance',{})
        return body.get('CollisionProfileName')!='NoCollision' and body.get('CollisionEnabled')!='ECollisionEnabled::NoCollision'

    def configure(component,source):
        component.set_mobility(unreal.ComponentMobility.STATIC)
        component.set_static_mesh(native_mesh(source['mesh']))
        configure_collision(component,source)
        component.set_editor_property('cast_shadow',source.get('properties',{}).get('CastShadow',True))
        component.set_editor_property('override_materials',[])
        translucent=False
        for i,material in material_bindings(component.static_mesh,source,slots):
            if material:
                native=native_material(material);component.set_material(i,native)
                if not native:raise RuntimeError('Native material unavailable: '+material)
                translucent |= native.get_base_material().get_editor_property('blend_mode')==unreal.BlendMode.BLEND_TRANSLUCENT
        component.set_editor_property('disallow_nanite',translucent)
        for i,value in enumerate(source.get('properties',{}).get('CustomPrimitiveData',{}).get('Data',[])):
            component.set_default_custom_primitive_data_float(i,float(value))

    for source in manifest['actors']:
        if region and source['region']!=region:continue
        transform=instance_transform(source)
        if source['existing'] or source['id'] in preserved or source['id'] in overrides['actor_transforms']:
            if source['id'] in preserved and 'SC_'+source['id'] not in existing:
                raise RuntimeError('Explicitly preserved actor is absent from this level: '+source['id'])
            actor=spawn('SC_'+source['id'],unreal.StaticMeshActor)
            if source['id'] not in preserved:
                actor.set_actor_transform(transform,False,True);configure(actor.static_mesh_component,source)
                actor.tags=list(set(actor.tags)|{unreal.Name('SpaceCenterRestoration')})
            records.append({'id':source['id'],'actor':actor.get_actor_label(),'instance':None})
        else:
            location=source['transform']['Translation']
            cell=[math.floor(location[k]/4000) for k in 'XY']
            properties=source.get('properties',{})
            key=json.dumps([source['mesh'],source['materials'],cell,properties.get('BodyInstance'),source.get('region'),
                            properties.get('CustomPrimitiveData'),properties.get('CastShadow',True)],sort_keys=True)
            groups[key].append((source,transform))

    if groups:
        cls=unreal.load_class(None,'/Script/BrokenEdge.SpaceCenterInstanceGroup')
        if not cls:raise RuntimeError('Build the Editor target to register SpaceCenterInstanceGroup')
        for index,(key,entries) in enumerate(sorted(groups.items())):
            actor=spawn('SCR_Group_'+g['identity'](key),cls)
            actor.root_component.set_mobility(unreal.ComponentMobility.STATIC)
            component=actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
            if not component:raise RuntimeError('Missing native instance component')
            component.clear_instances();configure(component,entries[0][0])
            # ClearInstances removes the old navigation entry. Refresh navigation
            # when repopulating an existing component as well as a new actor.
            component.add_instances([t for s,t in entries],False,True,True)
            assert component.get_instance_count()==len(entries)
            records.extend({'id':s['id'],'actor':actor.get_actor_label(),'instance':i} for i,(s,t) in enumerate(entries))
            if index%100==0:print('RESTORE_GROUP',index,'/',len(groups),flush=True)

    if region!='North':
        for tile in manifest['terrain']['tiles']:
            actor=spawn(tile['id'],unreal.StaticMeshActor)
            actor.set_actor_location(unreal.Vector(*tile['position']),False,True)
            component=actor.static_mesh_component
            component.set_static_mesh(lib.load_asset(state['terrain:'+tile['source']]['asset']))
            component.set_mobility(unreal.ComponentMobility.STATIC);component.set_collision_profile_name('BlockAll')
            if tile.get('material'):component.set_material(0,native_material(tile['material']))

    if map_path==PREVIEW:
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        world.get_world_settings().set_editor_property('default_game_mode',lib.load_blueprint_class('/Game/Characters/Stinger/Blueprints/BP_StingerTestGameMode'))
        sun=spawn('SCR_Sun',unreal.DirectionalLight,(0,0,6000),unreal.Rotator(pitch=-48,yaw=-35))
        component=sun.get_component_by_class(unreal.DirectionalLightComponent)
        component.set_mobility(unreal.ComponentMobility.MOVABLE);component.set_editor_property('intensity',5.)
        sky=spawn('SCR_Sky',unreal.SkyLight,(0,0,4000))
        sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        sky.light_component.set_editor_property('intensity',1.);sky.light_component.set_editor_property('real_time_capture',True)
        spawn('SCR_Atmosphere',unreal.SkyAtmosphere)
        fog=spawn('SCR_Fog',unreal.ExponentialHeightFog,(0,0,-1000))
        fog.component.set_editor_property('fog_density',.003)
        post=spawn('SCR_Exposure',unreal.PostProcessVolume);post.set_editor_property('unbound',True)
        settings=post.get_editor_property('settings')
        for k,v in [('override_auto_exposure_bias',True),('auto_exposure_bias',-1.),('override_bloom_intensity',True),('bloom_intensity',.15)]:settings.set_editor_property(k,v)
        post.set_editor_property('settings',settings)
        spawn('SCR_PlayerStart',unreal.PlayerStart,(-1500,-300,600))

    # Only this generator's tagged stale actors are removed; unrelated actors and explicit preservation survive.
    for label,actor in existing.items():
        if 'SpaceCenterRestoration' in [str(t) for t in actor.tags] and label not in desired and label.removeprefix('SC_') not in preserved:
            subsystem.destroy_actor(actor)
    if save:assert editor.save_current_level()
    report.update(map=map_path,geometry_sha256=geometry_signature(manifest),instances=len(records),groups=len(groups),terrain_tiles=0 if region=='North' else len(manifest['terrain']['tiles']))
    g['write_json'](g['OUT']/'ActorBindings.json',records)
    if map_path==PREVIEW:
        g['write_json'](g['WORK']/'PreviewBindings.json',{'geometry_sha256':geometry_signature(manifest),
            'bindings_file':str(g['OUT']/'ActorBindings.json')})
    return records


def run(g):
    if g['TASK']=='materials':
        from restoration_materials import create_materials
        create_materials(g)
    elif g['TASK']=='preview':assemble(g)
    elif g['TASK'] in ['audit','audit-integrated']:
        from restoration_audit import audit
        audit(g)
    elif g['TASK']=='integrate':
        from integrate_restoration import integrate
        integrate(g)
    else:raise RuntimeError('Unimplemented restoration stage: '+g['TASK'])
