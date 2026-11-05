"""Offline collision/navigation audit; does not claim player traversal coverage."""
import json
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir())/'Extraction/SovietMilitary'
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SovietMilitary/L_SovietMilitaryExtraction')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
helper=unreal.SovietMilitaryBuilder
definition=unreal.load_asset('/Game/Extraction/Maps/DA_SovietMilitaryGameplay')
backdrops=[c for a in actors.get_all_level_actors() if a.get_class().get_name()=='BP_TreesBackdropActor_C'
           for c in a.get_components_by_class(unreal.StaticMeshComponent)
           if c.static_mesh and c.static_mesh.get_name()=='SM_TreesBackdrop']
assert backdrops and all(c.get_editor_property('min_draw_distance')>=3000 for c in backdrops), 'Backdrop near culling did not survive reload'
print('SOVIET_BACKDROP_NEAR_CULL_VERIFIED',len(backdrops))
points=[]
for group in definition.get_editor_property('spawn_groups'):
    points.extend((str(group.id)+'_'+str(i),p.translation) for i,p in enumerate(group.positions))
points.extend((str(e.id),e.transform.translation) for e in definition.exits)
points.extend((str(p.id),p.transform.translation) for p in definition.loot_layout.points)
for actor in actors.get_all_level_actors():
    if isinstance(actor,unreal.ExtractionPatrolRoute):
        points.extend((actor.get_actor_label()+'_'+str(i),p) for i,p in enumerate(actor.points))

# Openable doors are portals. Test reachability both with closed and with open leaves.
report={}
for opened in (False,True):
    if opened:
        for actor in actors.get_all_level_actors():
            if isinstance(actor,unreal.ExtractionDoor):
                mesh=actor.get_editor_property('door')
                mesh.set_relative_rotation(unreal.Rotator(yaw=actor.get_editor_property('open_angle')),False,False)
    assert helper.build_navigation(world)
    start=points[0][1]
    results=[]
    for name,point in points:
        path=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,start,point)
        valid=bool((point-start).length()<1 or path and path.is_valid() and not path.is_partial())
        results.append(dict(id=name,reachable=valid,length=path.get_path_length() if path else 0,
            path=[[p.x,p.y,p.z] for p in path.path_points] if valid else []))
    report['open' if opened else 'closed']=results
(out/'NavigationAudit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
failed=[p['id'] for p in report['open'] if not p['reachable']]
print('SOVIET_NAVIGATION_AUDIT '+json.dumps(dict(points=len(points),failed=failed)))
assert not failed, 'Disconnected gameplay points: '+str(failed)
