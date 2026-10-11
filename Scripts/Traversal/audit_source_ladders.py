"""Read-only collision diagnostics at the saved original climb endpoints."""
import json
import os
from pathlib import Path
import sys
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0,str(ROOT/'Scripts/Traversal'))
from source_ladders import read,write_json,point
out=Path(os.environ['TRAVERSAL_EDITOR_OUT'])
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
data=read(ROOT/'Saved/Traversal/Sources.json')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
radius,half=34.,92.
def v(p):return unreal.Vector(*p)
def a(p):return [p.x,p.y,p.z]
def describe(hit):
    if not hit:return None
    fields=hit.to_tuple()
    actor,component=fields[9:11]
    return dict(block=fields[0],overlap=fields[1],distance=fields[3],location=a(fields[4]),impact=a(fields[5]),normal=a(fields[7]),
                actor=actor.get_actor_label() if actor else None,component=component.get_path_name() if component else None,
                item=fields[13],mesh=component.static_mesh.get_path_name() if isinstance(component,unreal.StaticMeshComponent) and component.static_mesh else None)
def floor(p):
    hit=unreal.SystemLibrary.line_trace_single_by_profile(world,v([p[0],p[1],p[2]+70]),v([p[0],p[1],p[2]-150]),'Pawn',False,[],unreal.DrawDebugTrace.NONE,True)
    return describe(hit)
def capsule(a,b):
    hit=unreal.SystemLibrary.capsule_trace_single_by_profile(world,v(a),v(b),radius,half,'Pawn',False,[],unreal.DrawDebugTrace.NONE,True)
    return describe(hit)
result=[]
for s in data['ladders']:
    ends={k:point(e['transform']) for k,e in s['endpoints'].items()}
    row=dict(id=s['id'],samples=[]);result.append(row)
    for key,p in ends.items():
        surface=floor(p)
        center=[p[0],p[1],(surface['impact'][2] if surface and surface['block'] else p[2])+half+3]
        row['samples'].append(dict(endpoint=key,point=p,floor=surface,capsule=capsule(center,[center[0],center[1],center[2]+.1]),
                                  footprint=[dict(offset=[x,y],hit=floor([p[0]+x,p[1]+y,p[2]])) for x,y in ((27.2,0),(-27.2,0),(0,27.2),(0,-27.2))]))
    b,t=[ends[k] for k in ('LadderBottomEndTransform','LadderTopEndTransform')]
    b=[b[0],b[1],b[2]+half+3];t=[t[0],t[1],t[2]+half+3]
    row['climb_sweep']=capsule(b,t)
    row['climb_column']=[dict(z=z,hit=capsule([b[0],b[1],z],[b[0],b[1],z+.1])) for z in range(int(b[2]),int(t[2]),100)]
report=dict(result='passed',ladders=result,bindings=[a.get_editor_property('source_id') for a in actors if isinstance(a,unreal.ExtractionLadder)])
write_json(out/'Report.json',report)
