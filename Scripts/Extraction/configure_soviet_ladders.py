"""Bind three climbable maintenance ladders to the central building's upper walkway."""
import json
import math
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir())
MAP='/Game/Maps/SovietMilitary/L_SovietMilitaryExtraction'
OWNER='SovietMaintenanceLaddersV1'
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(MAP)
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
native=json.loads((ROOT/'Saved/Traversal/SourceCorrection/NativeAssets.json').read_text(encoding='utf-8'))
meshes={k[-1]:unreal.load_asset(v['asset']) for k,v in native.items() if '/Tools/SM_' in k}
assert set(meshes)=={'A','B','C'}
capsule=unreal.get_default_object(unreal.load_class(None,'/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired.BP_StingerCharacter_Repaired_C')).get_editor_property('capsule_component')
radius,half=capsule.get_scaled_capsule_radius(),capsule.get_scaled_capsule_half_height()
for actor in actors.get_all_level_actors():
    if OWNER in [str(t) for t in actor.tags]: actors.destroy_actor(actor)
report={'radius':radius,'half':half,'ladders':[],'rejected':[]}
V=lambda a:unreal.Vector(*a)
A=lambda v:[v.x,v.y,v.z]

def clear(a,b): return unreal.StingerAssetLibrary.traversal_capsule_clear(world,a,b,radius,half)
def floor(p):
    point=unreal.SovietMilitaryBuilder.probe_ground(world,p,False)
    if point.z<=-10000:
        hit=unreal.StingerAssetLibrary.trace_traversal_surface(world,p+V((0,0,100)),p-V((0,0,1200)))
        print('LADDER_FLOOR_REJECT',A(p),[A(v) for v in hit] if hit else None,flush=True)
    return point if point.z>-10000 else None

def walk_clear(start,end):
    """Follow the floor and validate bounded step-up sweeps, as walking does on stairs."""
    previous=start
    for index in range(1,33):
        point=floor(start+(end-start)*(index/32.))
        if not point or abs(point.z-previous.z)>40:return False
        old=previous+V((0,0,half+3));new=point+V((0,0,half+3))
        raised_z=max(old.z,new.z)
        old_high=V((old.x,old.y,raised_z));new_high=V((new.x,new.y,raised_z))
        if not all([clear(old,old_high),clear(old_high,new_high),clear(new_high,new)]):return False
        previous=point
    return True

for side,angle,candidates in [
    ('North',-90,[(x,1800,850) for x in [500,-500,600,-600,400,-400]]),
    ('South',90,[(x,-1800,850) for x in [-500,500,-600,600,-400,400]]),
    ('West',0,[(-1800,y,850) for y in [500,-500,600,-600,400,-400]])]:
    chosen=None
    forward=V((math.cos(math.radians(angle)),math.sin(math.radians(angle)),0))
    up=V((0,0,half+3))
    for candidate in candidates:
        inward=V((math.cos(math.radians(angle)),math.sin(math.radians(angle)),0))
        start=V((candidate[0],candidate[1],1100))
        wall=unreal.StingerAssetLibrary.trace_traversal_surface(world,start,start+inward*900)
        if not wall or abs(wall[1].z)>.1:
            report['rejected'].append([side,candidate,'wall']);continue
        forward=wall[1]*-1
        foot_request=wall[0]-forward*65
        foot_request.z=850
        bottom=floor(foot_request)
        top_request=foot_request+forward*220
        top_request.z=1450
        exit_point=floor(top_request)
        if not bottom or not exit_point or not 1300<exit_point.z<1450 or exit_point.z-bottom.z<400:
            report['rejected'].append([side,candidate,'floor']);continue
        top=V((bottom.x,bottom.y,exit_point.z))
        approach=floor(bottom-forward*160)
        clearance=150.
        raised=top+up+V((0,0,clearance))
        across=exit_point+up+V((0,0,clearance))
        paths=[walk_clear(approach,bottom),clear(bottom+up,raised),clear(raised,across),clear(across,exit_point+up)] if approach else []
        if not approach or not all(paths):
            print('LADDER_PATH_REJECT',candidate,'capsule',radius,half,'floor',A(bottom),'exit',A(exit_point),'paths',paths,flush=True)
            if approach and not paths[0]:print('LADDER_BLOCKER',A(approach),unreal.StingerAssetLibrary.traversal_capsule_blocker(world,approach+up,bottom+up,radius,half),flush=True)
            report['rejected'].append([side,candidate,'capsule_path']);continue
        chosen=(bottom,top,exit_point,approach,clearance);break
    assert chosen, ('No safe ladder route',side,report['rejected'])
    bottom,top,exit_point,approach,clearance=chosen
    angle=math.degrees(math.atan2(forward.y,forward.x))
    ladder=actors.spawn_actor_from_class(unreal.ExtractionLadder,bottom,unreal.Rotator(yaw=angle))
    ladder.set_actor_label('Traversal_Soviet_'+side)
    ladder.tags=[OWNER]
    ladder.set_folder_path('Extraction/MaintenanceLadders')
    ladder.set_editor_property('source_id','Soviet_'+side)
    inverse=ladder.get_actor_transform().inverse()
    local=lambda p:unreal.MathLibrary.transform_location(inverse,p)
    height=top.z-bottom.z
    for field,point in [('bottom',bottom),('top',top),('bottom_exit',bottom-forward*35),('top_exit',exit_point)]:
        ladder.set_editor_property(field,local(point))
    ladder.set_editor_property('top_clearance',clearance)
    for field,point,extent in [('bottom_entry',bottom-forward*55+V((0,0,half)),(65,55,half+12)),
                               ('top_entry',exit_point+V((0,0,half)),(70,55,half+12))]:
        entry=unreal.TraversalLadderEntry()
        entry.set_editor_property('transform',unreal.Transform(location=local(point)))
        entry.set_editor_property('extent',V(extent))
        ladder.set_editor_property(field,entry)
    rung_heights=[48+38*i for i in range(6)]
    segments=[('A',0)]
    last=238
    while last+38<height+50:
        segments.append(('B',last));last+=38;rung_heights.append(last)
    segments.append(('C',last));rung_heights.append(last+38)
    # Source rung plane is local XY with its width along X; face it toward the roof.
    for i,(kind,z) in enumerate(segments):
        position=bottom+forward*55+V((0,0,z))
        mesh_actor=actors.spawn_actor_from_class(unreal.StaticMeshActor,position,unreal.Rotator(yaw=angle-90))
        mesh_actor.set_actor_label('Soviet_'+side+'_Ladder_'+str(i))
        mesh_actor.tags=[OWNER];mesh_actor.set_folder_path('Extraction/MaintenanceLadders')
        mesh_actor.static_mesh_component.set_static_mesh(meshes[kind])
        mesh_actor.static_mesh_component.set_collision_profile_name('NoCollision')
    ladder.set_editor_property('rungs',[V((55,0,z)) for z in rung_heights])
    ladder.set_editor_property('grip_origin',V((55,0,rung_heights[0])))
    ladder.set_editor_property('rung_spacing',38.)
    ladder.set_editor_property('grip_half_width',29.)
    report['ladders'].append(dict(id='Soviet_'+side,actor=ladder.get_actor_label(),bottom=A(bottom),top=A(top),
                                  bottom_exit=A(bottom-forward*35),top_exit=A(exit_point),approach=A(approach),yaw=angle,
                                  clearance=clearance,rungs=len(rung_heights)))
assert levels.save_current_level()
out=ROOT/'Saved/Extraction/Fix5767Ladders';out.mkdir(parents=True,exist_ok=True)
(out/'Report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('SOVIET_LADDERS_COMPLETE',len(report['ladders']))
