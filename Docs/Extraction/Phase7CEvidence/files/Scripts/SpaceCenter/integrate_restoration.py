"""Integrate a verified preview in memory; save only after gameplay/navigation checks."""
import json
import os
from pathlib import Path
import shutil
import unreal
from restoration_world import assemble,geometry_signature

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
    gates=read(root/'Scripts/SpaceCenter/restoration_acceptance.json')
    digest=g['sha'](os.environ['SPACECENTER_RESTORE_MANIFEST'])
    assert gates['manifest_sha256']==digest,'Acceptance manifest differs'
    for kind in ['audit','capture','traverse']:
        directory=root/gates[kind]
        run=read(directory/'Run.json');detail=read(directory/'Report.json')
        assert run['result']==detail['result']=='passed',(kind,'not passed')
        assert run['manifest_sha256']==digest,(kind,'stale manifest')
    assert gates['visual_review']=='passed','Review fixed-camera output before integrating'
    report['acceptance']=gates
    report['backups']=[]
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
    bounds.set_actor_location(unreal.Vector(2500,1500,2500),False,True)
    bounds.set_actor_scale3d(unreal.Vector(155,175,60))
    assert unreal.ExtractionNavigation.build_navigation(world),'Restored patrol navigation failed; no map has been saved'
    path=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,unreal.Vector(1400,5000,362),unreal.Vector(1400,10000,54))
    assert path and path.is_valid() and not path.is_partial(),'Restored north route is incomplete; no map has been saved'
    report['north_route']=[[p.x,p.y,p.z] for p in path.path_points]
    report['gameplay_actors_preserved']=len(before)
    stream.set_editor_property('initially_loaded',False);stream.set_editor_property('initially_visible',False)
    report.update(assemblies=assemblies,geometry_sha256=geometry_signature(manifest),instances=len(bindings))
    write(out/'ActorBindings.json',bindings)
    assert levels.save_all_dirty_levels()
    marker={'version':1,'manifest_sha256':digest,'geometry_sha256':geometry_signature(manifest),
            'manifest_file':str(Path(os.environ['SPACECENTER_RESTORE_MANIFEST']).relative_to(root)),
            'integration_report':str((out/'Report.json').relative_to(root)),
            'boundary':'Visual reconstruction; original stripped material graphs are not claimed recovered.'}
    write(root/'Scripts/SpaceCenter/restoration_active.json',marker)
    write(g['WORK']/'IntegrationBindings.json',{'bindings_file':str(out/'ActorBindings.json'),**marker})
