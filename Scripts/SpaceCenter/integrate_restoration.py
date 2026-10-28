"""Integrate a verified preview in memory; save only after gameplay/navigation checks."""
import json
import os
from pathlib import Path
import shutil
import unreal
from restoration_world import assemble,geometry_signature
from restoration_common import PROFILE, PROFILE_FILE, input_path

MAIN='/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
NORTH='/Game/Maps/SpaceCenter/L_SpaceCenterNorth'
FAKE_GROUND={'SC_RebuiltGround','SC_RebuiltForecourt','SC_NorthRebuiltGround','SC_NorthAccessRamp',
             'SC_NorthBoundaryNorth','SC_NorthBoundaryWest','SC_NorthBoundaryEast'}


def transform_values(actor):
    value=actor.get_actor_transform()
    return {name:[getattr(getattr(value,name),axis) for axis in axes]
            for name,axes in [('translation','xyz'),('rotation','xyzw'),('scale3d','xyz')]}


def integrate(g):
    read,write,root,out=g['read'],g['write_json'],g['ROOT'],g['OUT']
    manifest,report=g['MANIFEST'],g['report']
    gates=read(input_path('acceptance',root/'Scripts/SpaceCenter/restoration_acceptance.json'))
    geometry_only=PROFILE['phase'] in {'7E','7F'} and gates.get('integration_scope')=='scene_geometry'
    if PROFILE['phase'] in {'7E','7F'}:
        assert gates.get('status')==('ready_for_scene_integration' if geometry_only else 'ready'), 'Acceptance is not ready for the requested integration scope'
    digest=g['sha'](os.environ['SPACECENTER_RESTORE_MANIFEST'])
    assert gates['manifest_sha256']==digest,'Acceptance manifest differs'
    for kind in (['audit','capture'] if geometry_only else ['audit','capture','traverse']):
        directory=root/gates[kind]
        run=read(directory/'Run.json');detail=read(directory/'Report.json')
        assert run['result']==detail['result']=='passed',(kind,'not passed')
        assert run['manifest_sha256']==digest,(kind,'stale manifest')
    assert gates['visual_review']=='passed','Review fixed-camera output before integrating'
    if PROFILE['phase']=='7D':
        assert len(gates.get('east_navigation',[]))==2,'East navigation endpoints are required'
        capture=read(root/gates['capture']/'Report.json')
        review=read(root/gates['capture']/'VisualReview.json')
        assert review['result']=='passed' and len(review['captures'])>=20
        assert {c['file']:c['sha256'] for c in review['captures']}=={c['file']:c['sha256'] for c in capture['captures']}
        traverse=read(root/gates['traverse']/'Report.json')
        assert traverse['route_sha256']==g['sha'](input_path('traverse_route',root/'Scripts/SpaceCenter/route.json')),'Traversal route changed'
        assert {'east_arrival','east_return','room_return'}.issubset({c['name'] for c in traverse['checks'] if c['passed']})
    if PROFILE['phase'] in {'7E','7F'}:
        capture=read(root/gates['capture']/'Report.json')
        review=read(root/gates['capture']/'VisualReview.json')
        assert review['result']=='passed'
        assert {c['file']:c['sha256'] for c in review['captures']}=={c['file']:c['sha256'] for c in capture['captures']}
        required={'control_entry','control_upper','launch_arrival','launch_platform','region_return'}
        for kind in (['audit','capture'] if geometry_only else ['audit','capture','traverse','network']):
            assert read(root/gates[kind]/'Run.json').get('native_assets_sha256')==g['sha'](g['STATE_FILE']), 'Native assets changed after '+kind
        for kind,players in ([] if geometry_only else [('traverse',1),('network',2)]):
            directory=root/gates[kind]
            run=read(directory/'Run.json');detail=read(directory/'Report.json')
            assert run['result']==detail['result']=='passed' and run['manifest_sha256']==digest
            assert detail['players']==players
            assert detail['route']==read(input_path('traverse_route','phase7e_traverse_route.json'))
            passed={c['name'] for c in detail['checks'] if c['passed']}
            for player in range(1,players+1):
                assert {f'p{player}_{name}' for name in required}.issubset(passed)
                if players==2:assert {f'replicated_p{player}_{name}' for name in required}.issubset(passed)
    report['acceptance']=gates
    report['integration_scope']='scene_geometry' if geometry_only else 'validated_traversal'
    if geometry_only:
        assert len(gates.get('east_navigation',[]))==2,'Existing east navigation must remain covered'
        report['regional_traversal']='not_accepted'
        report['regional_network']='not_validated'
    report['backups']=[]
    for state in [root/'Scripts/SpaceCenter/restoration_active.json',g['WORK']/'IntegrationBindings.json']:
        if state.is_file():
            backup=out/'OriginalState'/state.name;backup.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(state,backup)
            report['backups'].append({'file':str(state.relative_to(root)),'backup':str(backup),'sha256':g['sha'](backup)})
    for name in [MAIN,NORTH]:
        source=root/'Content'/Path(name.removeprefix('/Game/')).with_suffix('.umap')
        backup=out/'OriginalMaps'/source.name;backup.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,backup)
        report['backups'].append({'file':str(source.relative_to(root)),'backup':str(backup),'sha256':g['sha'](backup)})
    write(out/'Before.json',report)
    levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert levels.load_level(MAIN)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    stream=unreal.GameplayStatics.get_streaming_level(world,NORTH)
    if not stream:stream=unreal.EditorLevelUtils.add_level_to_world(world,NORTH,unreal.LevelStreamingDynamic)
    assert stream and stream.get_loaded_level()
    unreal.EditorLevelUtils.set_level_visibility(stream.get_loaded_level(),True,False)
    original=list(actors.get_all_level_actors())
    gameplay=[a for a in original if a.get_class().get_name().startswith('Extraction') or a.get_actor_label().startswith('Phase6B_')]
    before={a.get_path_name():{'class':a.get_class().get_path_name(),'label':a.get_actor_label(),
        'transform':transform_values(a)} for a in gameplay}
    write(out/'GameplayBefore.json',before)
    previous=set()
    for path in [root/'Saved/SpaceCenter/ImportManifest.json',root/'Saved/SpaceCenter/Phase7A/ImportManifest.json']:
        previous.update(a['id'] for a in read(path)['actors'])
    manual=read(root/'Scripts/SpaceCenter/restoration_overrides.json')
    preserved=set(manual['preserve_actors'])|set(read(root/'Scripts/SpaceCenter/overrides.json')['preserve_actors'])
    individual={a['id']:a['region'] for a in manifest['actors'] if a['existing'] or a['id'] in preserved or a['id'] in manual['actor_transforms']}
    removed=[]
    for actor in original:
        label=actor.get_actor_label();ident=label.removeprefix('SC_')
        desired_level=NORTH if individual.get(ident)=='North' else MAIN
        stale_source=ident in previous and ident not in preserved and (ident not in individual or not actor.get_level().get_path_name().startswith(desired_level+'.'))
        if label in FAKE_GROUND or stale_source:
            assert isinstance(actor,unreal.StaticMeshActor),label
            removed.append(label);actors.destroy_actor(actor)
    report['removed_legacy_geometry']=removed
    bindings=[];assemblies=[]
    for region,path in [('North',NORTH),('Persistent',MAIN)]:
        stage=out/region;stage.mkdir()
        local=dict(g,OUT=stage,report={})
        rows=assemble(local,path,region,load=False,save=False)
        bindings.extend(row|{'map':path} for row in rows);assemblies.append(local['report'])
    assert len(bindings)==len(manifest['actors']) and len({b['id'] for b in bindings})==len(bindings)
    assert levels.set_current_level_by_name(MAIN.rsplit('/',1)[-1])
    native=list(actors.get_all_level_actors());by_path={a.get_path_name():a for a in native}
    for path,entry in before.items():
        assert path in by_path,'Gameplay actor removed: '+path
        actor=by_path[path]
        assert actor.get_class().get_path_name()==entry['class'] and transform_values(actor)==entry['transform'],'Gameplay actor changed: '+path
    bounds=next(a for a in native if a.get_actor_label()=='ExtractionNavigationBounds')
    from restoration_common import MINIMUM,MAXIMUM,ORIGIN
    low=[MINIMUM[i]-ORIGIN[i] for i in range(2)]
    high=[MAXIMUM[i]-ORIGIN[i] for i in range(2)]
    bounds.set_actor_location(unreal.Vector(*[(a+b)/2 for a,b in zip(low,high)],2500),False,True)
    bounds.set_actor_scale3d(unreal.Vector(*[(b-a+2000)/200 for a,b in zip(low,high)],60))
    assert unreal.ExtractionNavigation.build_navigation(world),'Restored patrol navigation failed; no map has been saved'
    path=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,unreal.Vector(1400,5000,362),unreal.Vector(1400,10000,54))
    assert path and path.is_valid() and not path.is_partial(),'Restored north route is incomplete; no map has been saved'
    report['north_route']=[[p.x,p.y,p.z] for p in path.path_points]
    if gates.get('east_navigation'):
        start,end=gates['east_navigation']
        east=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,unreal.Vector(*start),unreal.Vector(*end))
        assert east and east.is_valid() and not east.is_partial(),'Restored east route is incomplete; no map has been saved'
        report['east_route']=[[p.x,p.y,p.z] for p in east.path_points]
    for name,endpoints in gates.get('region_navigation',{}).items():
        start,end=endpoints
        route=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,unreal.Vector(*start),unreal.Vector(*end))
        assert route and route.is_valid() and not route.is_partial(), 'Incomplete '+name+' navigation; no map has been saved'
        report.setdefault('region_navigation',{})[name]=[[p.x,p.y,p.z] for p in route.path_points]
    report['gameplay_actors_preserved']=len(before)
    stream.set_editor_property('initially_loaded',False);stream.set_editor_property('initially_visible',False)
    report.update(assemblies=assemblies,geometry_sha256=geometry_signature(manifest),instances=len(bindings))
    write(out/'ActorBindings.json',bindings)
    assert levels.save_all_dirty_levels()
    marker={'version':1,'manifest_sha256':digest,'geometry_sha256':geometry_signature(manifest),
            'integration_scope':report['integration_scope'],
            'regional_traversal':report.get('regional_traversal','accepted'),
            'profile_file':PROFILE_FILE.relative_to(root).as_posix(),'profile_sha256':g['sha'](PROFILE_FILE),
            'manifest_file':str(Path(os.environ['SPACECENTER_RESTORE_MANIFEST']).relative_to(root)),
            'integration_report':str((out/'Report.json').relative_to(root)),
            'boundary':'Visual reconstruction; original stripped material graphs are not claimed recovered.'}
    write(root/'Scripts/SpaceCenter/restoration_active.json',marker)
    write(g['WORK']/'IntegrationBindings.json',{'bindings_file':str(out/'ActorBindings.json'),**marker})
