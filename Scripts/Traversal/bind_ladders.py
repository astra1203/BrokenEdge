"""Classify every restored ladder and bind only capsule-clear, supported entries/exits."""
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = Path(os.environ['TRAVERSAL_EDITOR_OUT'])
data = json.loads((ROOT/'Saved/Traversal/Sources.json').read_text(encoding='utf-8'))
rung_data = json.loads((ROOT/'Saved/Traversal/Rungs.json').read_text(encoding='utf-8'))
MAIN = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
NORTH = '/Game/Maps/SpaceCenter/L_SpaceCenterNorth'
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(MAIN)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
stream = unreal.GameplayStatics.get_streaming_level(world,NORTH)
if stream and stream.get_loaded_level():
    unreal.EditorLevelUtils.set_level_visibility(stream.get_loaded_level(),True,False)
trace = unreal.TraceTypeQuery.TRACE_TYPE_QUERY1
pawn_class=unreal.load_class(None,'/Game/Characters/Stinger/Blueprints/BP_StingerCharacter.BP_StingerCharacter_C')
capsule=unreal.get_default_object(pawn_class).get_editor_property('capsule_component')
radius,half=capsule.get_scaled_capsule_radius(),capsule.get_scaled_capsule_half_height()
last_ground={}
def vector(v): return unreal.Vector(*v)
def array(v): return [v.x,v.y,v.z]
def line(a,b):
    hit = unreal.StingerAssetLibrary.trace_traversal_surface(world,a,b)
    return [None,None,None,None,None,hit[0],None,hit[1],None,None] if hit else None
def clear(a,b):
    return unreal.StingerAssetLibrary.traversal_capsule_clear(world,a,b,radius,half)
def ground(point, above, below):
    global last_ground
    last_ground={'requested':array(point)}
    hit = line(point+unreal.Vector(0,0,above),point-unreal.Vector(0,0,below))
    if not hit or hit[7].z < .71:
        last_ground['failure']='no_floor';return None
    foot = hit[5]
    last_ground['foot']=array(foot)
    for x,y in ((radius*.8,0),(-radius*.8,0),(0,radius*.8),(0,-radius*.8)):
        edge = line(foot+unreal.Vector(x,y,12),foot+unreal.Vector(x,y,-12))
        if not edge or edge[7].z < .71 or abs(edge[5].z-foot.z) > 8:
            last_ground['failure']='footprint';return None
    center=foot+unreal.Vector(0,0,half+3)
    if not clear(center,center+unreal.Vector(0,0,.1)):
        last_ground.update(failure='capsule',point=array(center))
        return None
    return foot

report = dict(result='running', manifest_sha256=data['manifest_sha256'], candidates=[], radius=radius, half_height=half,
              boundary='Editor collision audit and bindings only; input-driven runtime and network acceptance are separate.')
control=line(unreal.Vector(-1500,-300,1000),unreal.Vector(-1500,-300,0))
assert control and abs(control[5].z-362)<15, 'Known formal-map floor did not trace correctly'
report['control_floor']=array(control[5])
for source in data['ladders']:
    t=source['transform'];q=unreal.Quat(*[t['Rotation'][k] for k in 'XYZW'])
    up=q.rotate_vector(unreal.Vector(0,0,1))
    row=dict(id=source['id'], source_object=source['source_object'], mesh=source['mesh'], region=source['region'], up=array(up))
    report['candidates'].append(row)
    if up.z < .85:
        row.update(classification='decoration',reason='horizontal_or_strongly_tilted');continue
    low=source['bounds']['min'][2]-data['origin'][2]
    high=source['bounds']['max'][2]-data['origin'][2]
    center=vector([(a+b)/2-o for a,b,o in zip(source['bounds']['min'],source['bounds']['max'],data['origin'])])
    # Source B is 28.6 cm deep on local X and 79.8 cm wide on local Y.
    # A is the folding version of the same ladder; its climbing faces also face +/- X.
    normal=q.rotate_vector(unreal.Vector(1,0,0));normal.z=0;normal=normal.normal()
    row['floor_samples']=[]
    for angle in range(0,360,45):
        for distance in (80,160,240):
            point=center+unreal.Vector(math.cos(math.radians(angle))*distance,math.sin(math.radians(angle))*distance,0)
            hit=line(unreal.Vector(point.x,point.y,high+180),unreal.Vector(point.x,point.y,low-180))
            row['floor_samples'].append(dict(x=point.x,y=point.y,hit=array(hit[5]) if hit else None,
                                            normal=array(hit[7]) if hit else None,
                                            actor=hit[9].get_actor_label() if hit and hit[9] else None))
    selected=None;failures={};row['endpoint_probes']=[]
    for side in (1,-1):
        facing=normal*side
        for offset in (75.,95.,115.):
            climb=center-facing*offset
            bottom=ground(unreal.Vector(climb.x,climb.y,low),60,120)
            row['endpoint_probes'].append({'end':'bottom',**last_ground})
            if bottom is None:
                failures['bottom_unsupported_or_blocked']=failures.get('bottom_unsupported_or_blocked',0)+1;continue
            tangent=unreal.Vector(-facing.y,facing.x,0)
            exit_offsets=[(d,l) for d in (50.,70.,90.,110.,150.) for l in (0.,-45.,45.,-90.,90.)]
            for exit_distance,lateral in exit_offsets:
                target=center+facing*exit_distance+tangent*lateral
                top=ground(unreal.Vector(target.x,target.y,high),65,90)
                row['endpoint_probes'].append({'end':'top',**last_ground})
                if top is None or top.z-bottom.z < 150:
                    failures['top_unsupported_or_blocked']=failures.get('top_unsupported_or_blocked',0)+1;continue
                b=unreal.Vector(climb.x,climb.y,bottom.z)
                e=unreal.Vector(climb.x,climb.y,top.z)
                if not clear(b+unreal.Vector(0,0,half+3),e+unreal.Vector(0,0,half+8)) or not clear(e+unreal.Vector(0,0,half+8),top+unreal.Vector(0,0,half+8)):
                    failures['climb_or_exit_path_blocked']=failures.get('climb_or_exit_path_blocked',0)+1;continue
                selected=dict(bottom=array(b),top=array(e),bottom_exit=array(bottom),top_exit=array(top),facing=array(facing),
                              source_side=side,grip_origin=array(unreal.Vector(center.x,center.y,low+15)-facing*14));break
            if selected:break
        if selected:break
    if selected:
        row.update(classification='usable',points=selected)
    else:
        row.update(classification='unusable',reason='no_safe_connected_endpoints',failures=failures)
    print('TRAVERSAL_LADDER',row['id'],row['classification'],flush=True)

# Save an exact backup before adding only our invisible interaction actors.
mapfile=ROOT/'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap'
shutil.copy2(mapfile,OUT/'L_SpaceCenterExtraction.before.umap')
report['map_sha256_before']=hashlib.sha256(mapfile.read_bytes()).hexdigest()
existing={a.get_editor_property('source_id'):a for a in actors.get_all_level_actors() if isinstance(a,unreal.ExtractionLadder)}
for row in report['candidates']:
    if row['classification']!='usable':continue
    p=row['points'];facing=vector(p['facing']);yaw=math.degrees(math.atan2(facing.y,facing.x))
    rotation=unreal.Rotator(pitch=0.,yaw=yaw,roll=0.)
    actor=existing.pop(row['id'],None) or actors.spawn_actor_from_class(unreal.ExtractionLadder,vector(p['bottom']),rotation)
    actor.set_actor_location_and_rotation(vector(p['bottom']),rotation,False,True)
    actual=actor.get_actor_forward_vector()
    assert abs(actual.z)<.001 and actual.dot(facing)>.999, 'Binding must face horizontally along the climbing surface'
    row['binding_forward']=array(actual)
    actor.set_actor_label('Traversal_Ladder_'+row['id'])
    actor.set_editor_property('source_id',row['id'])
    inverse=actor.get_actor_transform().inverse()
    for key in ('bottom','top','bottom_exit','top_exit','grip_origin'):
        actor.set_editor_property(key,unreal.MathLibrary.transform_location(inverse,vector(p[key])))
    source=next(s for s in data['ladders'] if s['id']==row['id'])
    t=source['transform'];q=unreal.Quat(*[t['Rotation'][k] for k in 'XYZW'])
    scale=vector([t['Scale3D'][k] for k in 'XYZ'])
    translation=vector([t['Translation'][k]-o for k,o in zip('XYZ',data['origin'])])
    geometry=rung_data[source['mesh']]
    rungs=[]
    for rung in geometry['rungs']:
        x=rung['min'][0] if p['source_side']>0 else rung['max'][0]
        local=unreal.Vector(x,(rung['min'][1]+rung['max'][1])/2,rung['z'])
        point=q.rotate_vector(local*scale)+translation
        rungs.append(point)
    rungs.sort(key=lambda p:p.z)
    actor.set_editor_property('rungs',[unreal.MathLibrary.transform_location(inverse,p) for p in rungs])
    actor.set_editor_property('grip_origin',unreal.MathLibrary.transform_location(inverse,rungs[0]))
    actor.set_editor_property('rung_spacing',geometry['median_spacing']*abs(t['Scale3D']['Z'])*abs(q.rotate_vector(unreal.Vector(0,0,1)).z))
    actor.set_editor_property('grip_half_width',min(22.,min((r['max'][1]-r['min'][1])/2-6 for r in geometry['rungs'])))
    row['contact_rungs']=[array(p) for p in rungs]
    row['actor']=actor.get_path_name()
for obsolete in existing.values():actors.destroy_actor(obsolete)
if existing or any(r['classification']=='usable' for r in report['candidates']):
    assert levels.save_current_level()
report['map_sha256_after']=hashlib.sha256(mapfile.read_bytes()).hexdigest()
report['counts']={kind:sum(r['classification']==kind for r in report['candidates']) for kind in ('usable','unusable','decoration')}
report['result']='passed'
(OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
assert sum(report['counts'].values())==39
