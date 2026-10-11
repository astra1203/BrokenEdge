"""Find a walking connection from the existing mantle segment to a true ladder."""
import heapq
import math
import os
from pathlib import Path
import sys
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0,str(ROOT/'Scripts/Traversal'))
from source_ladders import read,write_json,sha
out=Path(os.environ['TRAVERSAL_EDITOR_OUT'])
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
bindings=read(ROOT/'Scripts/Traversal/source_bindings.json')
assert bindings['map_sha256']==sha(ROOT/'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap')
ladder=next(r for r in bindings['candidates'] if r['id']=='9cf77452749b621c' and r['classification']=='usable')
target=ladder['approaches']['top']
start_point=[-3700,-2050,360.33]
radius,half=34.,92.
def v(p):return unreal.Vector(*p)
def clear(a,b):return unreal.StingerAssetLibrary.traversal_capsule_clear(world,v(a),v(b),radius,half)
nodes={};cells={}

def walkable_link(a,b):
    if abs(b[2]-a[2])>45:return False
    height=max(a[2],b[2])+half+3
    raised,across=[a[0],a[1],height],[b[0],b[1],height]
    return (clear([a[0],a[1],a[2]+half+3],raised) and clear(raised,across)
            and clear(across,[b[0],b[1],b[2]+half+3]))
def cell(x,y):
    if (x,y) in cells:return cells[x,y]
    found=[];cells[x,y]=found
    if not (-4700<=x<=1500 and -7600<=y<=-1500):return found
    for z in (550,850):
        hit=unreal.StingerAssetLibrary.trace_traversal_surface(world,unreal.Vector(x,y,z),unreal.Vector(x,y,300))
        if not hit or hit[1].z<.71:continue
        p=[hit[0].x,hit[0].y,hit[0].z];center=[p[0],p[1],p[2]+half+3]
        key=(x,y,round(p[2]/5))
        if key not in nodes and clear(center,[center[0],center[1],center[2]+.1]):
            nodes[key]=p;found.append(key)
    return found
def nearest(p):
    cx,cy=[round(n/50)*50 for n in p[:2]]
    candidates=[k for dx in (-50,0,50) for dy in (-50,0,50) for k in cell(cx+dx,cy+dy)]
    candidates=[k for k in candidates if walkable_link(p,nodes[k]) and walkable_link(nodes[k],p)]
    assert candidates, 'No walking connector at '+str(p)
    return min(candidates,key=lambda k:math.dist(nodes[k],p))
start=nearest(start_point);end=nearest(target)
queue=[(0,start)];cost={start:0};parent={start:None}
expanded=set()
while queue:
    _,key=heapq.heappop(queue)
    if key==end:break
    if key in expanded:continue
    expanded.add(key)
    # Probe only cells reached by A*, with a hard bound on resident physics data.
    if len(expanded)>=2000:break
    a=nodes[key]
    for dx,dy in ((50,0),(-50,0),(0,50),(0,-50),(50,50),(-50,50),(50,-50),(-50,-50)):
        for other in cell(key[0]+dx,key[1]+dy):
            b=nodes[other]
            candidate=cost[key]+math.dist(a,b)
            if candidate>=cost.get(other,math.inf):continue
            if not walkable_link(a,b):continue
            cost[other]=candidate;parent[other]=key
            heapq.heappush(queue,(candidate+math.dist(b,nodes[end]),other))
route=[]
if end in parent:
    key=end
    while key is not None:
        route.append(nodes[key]);key=parent[key]
    route.reverse()
    route=[start_point]+route+[target]
report=dict(result='passed',connection=bool(route),start=nodes[start],target=target,end=nodes[end],route=route,nodes=len(nodes),expanded=len(expanded),
            closest=nodes[min(parent,key=lambda k:math.dist(nodes[k],target))],source_id=ladder['id'],
            map_sha256=bindings['map_sha256'],binding_sha256=sha(ROOT/'Scripts/Traversal/source_bindings.json'),
            boundary='Collision-derived walking proposal. Actual-input continuous-route acceptance remains required.')
write_json(out/'Report.json',report)
if route:
    routefile=ROOT/'Scripts/Traversal/formal_route.json'
    formal=read(routefile)
    write_json(out/'FormalRoute.before.json',formal)
    formal.update(status='ready',basis='Original actor and capsule-tested walking proposal; actual-input acceptance pending',
                  binding_sha256=report['binding_sha256'],collision_report=str(out/'Report.json'),
                  ladder=dict(source_id=ladder['id'],entry='top'),to_ladder=route)
    write_json(routefile,formal)
