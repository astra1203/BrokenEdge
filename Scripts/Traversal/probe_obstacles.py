"""Read-only production-shape probes on existing source-map surfaces."""
import json
import math
import os
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=Path(os.environ['TRAVERSAL_EDITOR_OUT'])
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
radius,half=34,92
def v(p):return unreal.Vector(*p)
def hit(a,b):
    r=unreal.StingerAssetLibrary.trace_traversal_surface(world,v(a),v(b))
    return ([r[0].x,r[0].y,r[0].z],[r[1].x,r[1].y,r[1].z]) if r else None
def clear(a,b):return unreal.StingerAssetLibrary.traversal_capsule_clear(world,v(a),v(b),radius,half)
def add(p,d,n=1):return [a+b*n for a,b in zip(p,d)]
def stand(p):
    for dx,dy in ((0,0),(27.2,0),(-27.2,0),(0,27.2),(0,-27.2)):
        a=add(p,[dx,dy,0]);r=hit(add(a,[0,0,12]),add(a,[0,0,-12]))
        if not r or r[1][2]<.71 or abs(r[0][2]-p[2])>8:return False
    a=add(p,[0,0,95]);return clear(a,add(a,[0,0,.1]))
def nav(a,b):
    p=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,v(a),v(b))
    return [[q.x,q.y,q.z] for q in p.path_points] if p and not p.is_partial() and len(p.path_points)>1 else []
nodes=json.loads((ROOT/'Saved/Traversal/EditorRuns/TraversalRoute05/Nodes.json').read_text())
candidates=[]
for row in nodes:
    feet=row['point']
    if not 340<feet[2]<400:continue
    if any(math.dist(feet,c['start'])<125 for c in candidates):continue
    for yaw,d in ((0,[1,0,0]),(90,[0,1,0]),(180,[-1,0,0]),(-90,[0,-1,0])):
        probe=add(feet,[0,0,51]);front=hit(probe,add(probe,d,139))
        if not front or sum(-a*b for a,b in zip(front[1],d))<.5:continue
        lip=add(front[0],d,5)
        top=hit([lip[0],lip[1],feet[2]+192],[lip[0],lip[1],feet[2]+45])
        if not top or top[1][2]<.71:continue
        height=top[0][2]-feet[2]
        if not 50<height<=180.1:continue
        end=None;kind='mantle'
        if height<=120.1:
            for depth in range(10,101,5):
                point=add(front[0],d,depth);point[2]=top[0][2]
                if hit(add(point,[0,0,8]),add(point,[0,0,-12])):continue
                point=add(point,d,40);landing=hit(point,[point[0],point[1],feet[2]-30])
                if landing and landing[1][2]>.71 and abs(landing[0][2]-feet[2])<=45 and stand(landing[0]):
                    end=landing[0];kind='vault'
                break
        if end is None:
            end=add(top[0],d,42)
            if not stand(end):continue
        start=add(feet,[0,0,95]);raised=[start[0],start[1],top[0][2]+97]
        center=add(end,[0,0,95]);across=[center[0],center[1],raised[2]]
        if not all(clear(a,b) for a,b in zip([start,raised,across],[raised,across,center])):continue
        approach=nav([1200,-1400,363],feet)
        departure=nav(end,[-4003.65,-2196.24,360.68])
        if not approach:continue
        candidates.append(dict(kind=kind,height=height,start=feet,end=end,yaw=yaw,approach=approach,departure=departure))
        break
report=dict(result='passed',candidates=candidates,boundary='Geometry probes only; production input is required for acceptance.')
(OUT/'Report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
