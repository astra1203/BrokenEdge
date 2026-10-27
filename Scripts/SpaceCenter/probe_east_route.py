"""Measure a capsule-clear east floor graph in PIE; actual walking remains a separate gate."""
import collections
import math
import os
from pathlib import Path
import sys
import time
import traceback
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve();sys.path.insert(0,str(ROOT/'Scripts/SpaceCenter'))
from restoration_common import read,write_json,sha,WORK
from plan_route import FloorGraph
OUT=Path(os.environ['SPACECENTER_RUN_DIR'])
CONFIG_FILE=Path(os.environ.get('SPACECENTER_PROBE_CONFIG',str(ROOT/'Scripts/SpaceCenter/east_probe.json')));config=read(CONFIG_FILE)
LIB=unreal.StingerAssetLibrary
state={'phase':'boot','elapsed':0.};started=time.monotonic()
report={'result':'running','config':config,'config_sha256':sha(CONFIG_FILE)}


def finish(error=None):
    report.update(result='failed' if error else 'passed',elapsed_seconds=time.monotonic()-started)
    if error:report['error']=error
    write_json(OUT/'Report.json',report)
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def measure(pawn):
    world=pawn.get_world();probes=[];step=config['step_cm'];assert step==100
    if config.get('seed_probe_directory'):
        seed=ROOT/config['seed_probe_directory']
        assert read(seed/'Run.json')['manifest_sha256']==sha(os.environ['SPACECENTER_RESTORE_MANIFEST'])
        assert read(seed/'Run.json').get('native_assets_sha256')==sha(WORK/'NativeAssets.json'),'Seed collision asset ledger differs'
        assert read(seed/'Report.json')['result']=='passed'
        assert read(seed/'Report.json')['config'].get('collision_channel','visibility')==config.get('collision_channel','visibility')
        probes=read(seed/'Probes.json')
    regions=config.get('regions',[config])
    cells=collections.defaultdict(set)
    for region in regions:
        for x in range(region['minimum'][0],region['maximum'][0]+1,step):
            cells[x].update(range(region['minimum'][1],region['maximum'][1]+1,step))
    for x,ys in sorted(cells.items()):
        for y in sorted(ys):
            seen=set()
            for height in config['trace_heights']:
                hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,height),unreal.Vector(x,y,config['trace_bottom']),
                    unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[pawn],unreal.DrawDebugTrace.NONE)
                if not hit:continue
                v=hit.to_tuple();point,normal=v[5],v[7];key=round(point.z/5)
                if normal.z<=.65 or key in seen:continue
                seen.add(key)
                clearance=unreal.SystemLibrary.capsule_trace_single(world,point+unreal.Vector(0,0,135),point+unreal.Vector(0,0,136),
                    42.,96.,unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[pawn],unreal.DrawDebugTrace.NONE)
                probes.append({'location':[point.x,point.y,point.z],'normal':[normal.x,normal.y,normal.z],
                               'clear':clearance is None,'actor':v[9].get_actor_label() if v[9] else None})
        if x%1000==0:print('EAST_FLOOR_PROBE',x,len(probes),flush=True)
        yield
    write_json(OUT/'Probes.json',probes)
    graph=FloorGraph(probes);blocked=[];tested=set()
    for index,key in enumerate(graph.nodes):
        for other in graph.adjacent(key):
            edge=frozenset((key,other));a,b=graph.nodes[key]['location'],graph.nodes[other]['location']
            if edge in tested:continue
            tested.add(edge)
            va,vb=unreal.Vector(*a)+unreal.Vector(0,0,135),unreal.Vector(*b)+unreal.Vector(0,0,135)
            hit=unreal.SystemLibrary.capsule_trace_single(world,va,vb,44.,94.,unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[pawn],unreal.DrawDebugTrace.NONE)
            if not hit:hit=unreal.SystemLibrary.capsule_trace_single(world,vb,va,44.,94.,unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[pawn],unreal.DrawDebugTrace.NONE)
            if hit:
                v=hit.to_tuple()
                if v[5].z-min(a[2],b[2])<45:continue
                blocked.append({'from':key,'to':other,'actor':v[9].get_actor_label() if v[9] else None})
        if index%100==0:yield
    write_json(OUT/'BlockedEdges.json',blocked)
    graph=FloorGraph(probes,blocked);start=graph.near(config['start']);reached={start};queue=collections.deque([start])
    assert math.dist(graph.nodes[start]['location'],config['start'])<30,'No measured connection to old route'
    while queue:
        for neighbor in graph.adjacent(queue.popleft()):
            if neighbor not in reached:reached.add(neighbor);queue.append(neighbor)
    report.update(probes=len(probes),tested_edges=len(tested),blocked_edges=len(blocked),reachable_nodes=len(reached),
        reachable_bounds=[[min(graph.nodes[n]['location'][i] for n in reached),max(graph.nodes[n]['location'][i] for n in reached)] for i in range(3)])
    if config.get('survey_only'):
        report['targets']=[{'name':t['name'],'requested':t['location'],
            'nearest_reachable':graph.nodes[min(reached,key=lambda n:math.dist(graph.nodes[n]['location'],t['location']))]}
            for t in config['targets']]
        report['boundary']='Measured collision graph only; actual input-driven traversal remains required.'
        return
    candidates=[n for n in reached if graph.nodes[n]['location'][0]>=22000]
    assert candidates,'No capsule-clear east connection found in measured corridor'
    end=min(candidates,key=lambda n:math.dist(graph.nodes[n]['location'],config['desired_end']))
    path=graph.path(config['start'],graph.nodes[end]['location']);points=[p['location'] for p in path]
    old=read(ROOT/'Scripts/SpaceCenter/route.json');segments=old['segments'][:-1]+[
        {'name':'east_arrival','points':points,'floor_actors':sorted({p['actor'] for p in path if p['actor']})},
        {'name':'east_return','points':list(reversed(points))},old['segments'][-1]]
    write_json(OUT/'TraverseRoute.json',old|{'segments':segments})
    write_json(OUT/'NetworkRoute.json',{'outward':[p for s in segments[:-2] for p in s['points']],
        'homeward':[p for s in segments[-2:] for p in s['points']],
        'probe_report':str((OUT/'Report.json').relative_to(ROOT)),'measurement_only':True})
    report.update(probes=len(probes),tested_edges=len(tested),blocked_edges=len(blocked),reachable_nodes=len(reached),
        east_start=points[0],east_end=points[-1],east_distance_cm=sum(math.dist(a,b) for a,b in zip(points,points[1:])),
        source_route_sha256=sha(ROOT/'Scripts/SpaceCenter/route.json'),boundary='Measured floor/capsule graph only; requires input-driven traversal and packaged replication acceptance.')


def tick(delta):
    try:
        state['elapsed']+=delta
        if time.monotonic()-started>1200:raise TimeoutError('East floor probe timed out')
        if state['phase']=='boot' and state['elapsed']>5:
            unreal.SystemLibrary.execute_console_command(None,'Editor.AsyncAssetCompilationFinishAll')
            unreal.SystemLibrary.collect_garbage();LIB.begin_validation_pie(1);state.update(phase='spawn',elapsed=0)
        elif state['phase']=='spawn':
            pawns=[p for w in LIB.get_validation_worlds() for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BrokenEdgeCharacter) if p.is_locally_controlled()]
            if len(pawns)!=1 or state['elapsed']<5:return
            pawn=pawns[0];LIB.isolate_validation_input(pawn.get_controller())
            from restoration_terrain_audit import audit
            terrain=audit(pawn.get_world(),read(os.environ['SPACECENTER_RESTORE_MANIFEST']))
            write_json(OUT/'TerrainCollision.json',terrain)
            report['terrain_collision']={k:v for k,v in terrain.items() if k!='probes'}
            assert terrain['result']=='passed','Original terrain collision differs'
            if config.get('collision_channel')=='pawn':
                # Query the actual Pawn responses through the available trace
                # channel in this disposable PIE world. Never save these changes.
                changed=0
                for actor in unreal.GameplayStatics.get_all_actors_of_class(pawn.get_world(),unreal.Actor):
                    for component in actor.get_components_by_class(unreal.PrimitiveComponent):
                        response=component.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)
                        if response!=component.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY):
                            component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY,response)
                            changed+=1
                report['pawn_response_query']={'temporary_components':changed,'saved':False}
            unreal.SystemLibrary.execute_console_command(pawn,'t.MaxFPS 30',pawn.get_controller())
            state.update(phase='measure',generator=measure(pawn))
        elif state['phase']=='measure':
            try:next(state['generator'])
            except StopIteration:finish()
    except Exception:finish(traceback.format_exc())


handle=unreal.register_slate_post_tick_callback(tick)
