"""Assign native ownership of regenerated legacy panes; remove duplicate baked frames."""
import hashlib
import json
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir())
level='/Game/Maps/SovietMilitary/L_SovietMilitaryExtraction'
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level(level)
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
all_actors=list(actors.get_all_level_actors())
labels={a.get_actor_label():a for a in all_actors}
native=[a for a in all_actors if isinstance(a,unreal.ExtractionBreakableGlass)]
rows=[]
legacy_frames=[]
def same_mesh_transform(a,b):
    if a.static_mesh!=b.static_mesh:return False
    for getter in ['get_world_location','get_world_scale']:
        left,right=getattr(a,getter)(),getattr(b,getter)()
        if sum((getattr(left,axis)-getattr(right,axis))**2 for axis in ['x','y','z'])>.01:return False
    left,right=a.get_world_rotation(),b.get_world_rotation()
    return all(abs((getattr(left,axis)-getattr(right,axis)+180)%360-180)<.01 for axis in ['pitch','yaw','roll'])
for actor in all_actors:
    components=actor.get_components_by_class(unreal.StaticMeshComponent)
    if any(c.get_name().endswith('_StaticGlass') and c.static_mesh for c in components):
        legacy_frames.extend(c for c in components if c.static_mesh and not c.get_name().endswith('_StaticGlass'))
    for component in components:
        if not component.get_name().endswith('_StaticGlass') or not component.static_mesh:continue
        key=actor.get_path_name()+'.'+component.get_name()
        p=component.get_world_location()
        matches=[a for a in native if a.get_editor_property('pane').static_mesh==component.static_mesh and
                 sum((getattr(a.get_actor_location(),axis)-getattr(p,axis))**2 for axis in ['x','y','z'])<.01]
        assert len(matches)==1,(key,len(matches))
        replacement=matches[0]
        replacement.set_editor_property('replaces_legacy_window',True)
        rows.append(dict(source=key,replacement=replacement.get_actor_label(),id=unreal.GuidLibrary.conv_guid_to_string(replacement.get_editor_property('pane_id'))))
assert 0<len(rows)<=196,len(rows)
for pane in native:pane.set_editor_property('replaces_legacy_window',True)
removed=0
for actor in all_actors:
    if isinstance(actor,unreal.StaticMeshActor) and str(actor.get_folder_path())=='Extraction/WindowFrames' and any(same_mesh_transform(actor.static_mesh_component,c) for c in legacy_frames):
        assert actors.destroy_actor(actor)
        removed+=1
assert levels.save_current_level()
assert levels.load_level('/Game/Maps/Lobby')
assert levels.load_level(level)
panes=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ExtractionBreakableGlass)]
assert len(panes)==196 and all(a.get_editor_property('replaces_legacy_window') for a in panes)
(root/'Saved/Extraction/Fix5767Glass/LegacyWindowRepair.json').write_text(json.dumps(dict(panes=rows,duplicate_frames_removed=removed),indent=2),encoding='utf8')
print('SOVIET_LEGACY_WINDOW_REPLACEMENTS',len(rows),'FRAMES_REMOVED',removed)
