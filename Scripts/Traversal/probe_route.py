"""Read-only formal-map floor graph between the old stairs and bound source ladder."""
import collections
import heapq
import json
import math
import os
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=Path(os.environ['TRAVERSAL_EDITOR_OUT'])
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
trace=unreal.TraceTypeQuery.TRACE_TYPE_QUERY1
pawn_class=unreal.load_class(None,'/Game/Characters/Stinger/Blueprints/BP_StingerCharacter.BP_StingerCharacter_C')
capsule=unreal.get_default_object(pawn_class).get_editor_property('capsule_component')
radius,half=capsule.get_scaled_capsule_radius(),capsule.get_scaled_capsule_half_height()
def v(p):return unreal.Vector(*p)
def line(a,b):
    hit=unreal.StingerAssetLibrary.trace_traversal_surface(world,v(a),v(b))
    return [None,None,None,None,None,hit[0],None,hit[1],None,None] if hit else None
def clear(a,b):
    return unreal.StingerAssetLibrary.traversal_capsule_clear(world,v(a),v(b),radius,half)
nodes={};cells=collections.defaultdict(list)
for x in range(-4250,1701,50):
    for y in range(-3500,4501,50):
        for z in (600,850):
            hit=line([x,y,z],[x,y,300])
            if not hit or hit[7].z<.71:continue
            p=hit[5];point=[p.x,p.y,p.z]
            if clear([p.x,p.y,p.z+half+3],[p.x,p.y,p.z+half+3.1]):
                key=(x,y,round(p.z/5));nodes[key]=point
for key in nodes:cells[key[:2]].append(key)
edges={};actions={};obstacles=[]
for key,a in nodes.items():
    edges[key]=[]
    for dx,dy in ((50,0),(-50,0),(0,50),(0,-50),(50,50),(-50,50),(50,-50),(-50,-50)):
        for other in cells[(key[0]+dx,key[1]+dy)]:
            b=nodes[other];rise=b[2]-a[2]
            if abs(rise)>180:continue
            # Horizontal sweep above the higher foot surface; rises over native step are explicit mantle inputs.
            h=max(a[2],b[2])+half+3
            if not clear([a[0],a[1],h],[b[0],b[1],h]):continue
            if not clear([a[0],a[1],a[2]+half+3],[a[0],a[1],h]):continue
            if not clear([b[0],b[1],h],[b[0],b[1],b[2]+half+3]):continue
            edges[key].append(other)
            if rise>45:actions[(key,other)]='mantle'
start=min(nodes,key=lambda k:math.dist(nodes[k],[1200,-1400,363]))
end=min(nodes,key=lambda k:math.dist(nodes[k],[-4003.65,-2196.24,360.68]))
queue=[(0,start)];costs={start:0};parents={start:None}
while queue:
    _,key=heapq.heappop(queue)
    if key==end:break
    for other in edges[key]:
        cost=costs[key]+math.dist(nodes[key],nodes[other])+(200 if (key,other) in actions else 0)
        if cost<costs.get(other,math.inf):
            costs[other]=cost;parents[other]=key
            heapq.heappush(queue,(cost+math.dist(nodes[other],nodes[end]),other))
route=[]
if end in parents:
    key=end
    while key is not None:
        previous=parents[key]
        route.append(dict(point=nodes[key],action=actions.get((previous,key),'walk')))
        key=previous
    route.reverse()
report=dict(result='passed',nodes=len(nodes),connection=bool(route),start=nodes[start],end=nodes[end],route=route,
            reached=len(parents),closest_reachable=nodes[min(parents,key=lambda k:math.dist(nodes[k],nodes[end]))],
            boundary='Measured geometry route only. All manual traversal edges and walking must pass actual input acceptance.')
nav=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,v(nodes[start]),v(nodes[end]))
report['nav_path']=[[p.x,p.y,p.z] for p in nav.path_points] if nav else []
report['nav_partial']=nav.is_partial() if nav else True
old=json.loads((ROOT/'Scripts/SpaceCenter/route.json').read_text(encoding='utf-8'))
report['old_route_blocked_edges']=[]
for segment in old['segments']:
    if segment['name'] not in ('door','north_exit','room_return'):continue
    for index,(a,b) in enumerate(zip(segment['points'],segment['points'][1:])):
        h=max(a[2],b[2])+half+3
        if not clear([a[0],a[1],h],[b[0],b[1],h]):
            report['old_route_blocked_edges'].append(dict(segment=segment['name'],index=index,from_point=a,to_point=b))
animation=unreal.load_asset('/Game/Characters/Stinger/Animations/ThirdPerson/ABP_BrokenEdgeAnimInstance_Stinger')
report['body_slots']=[]
for index in range(30):
    node=unreal.find_object(None,animation.get_path_name()+':AnimGraph.AnimGraphNode_Slot_'+str(index))
    if node:
        report['body_slots'].append(dict(path=node.get_path_name(),slot=str(node.get_editor_property('node').get_editor_property('slot_name'))))
(OUT/'Report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
(OUT/'Nodes.json').write_text(json.dumps([dict(key=k,point=p) for k,p in nodes.items()]),encoding='utf-8')
