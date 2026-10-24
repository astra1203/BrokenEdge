"""Build a world-coordinate overview from restored floor collision bounds, not invented map art."""
import json
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir());lib=unreal.EditorAssetLibrary
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rectangles={}
for level in ['/Game/Maps/SpaceCenter/L_SpaceCenterExtraction','/Game/Maps/SpaceCenter/L_SpaceCenterNorth','/Game/Maps/SpaceCenter/L_SpaceCenterEast']:
    if not lib.does_asset_exist(level):continue
    assert levels.load_level(level);unreal.StingerAssetLibrary.finish_validation_asset_compilation()
    for actor in actors.get_all_level_actors():
        component=actor.get_component_by_class(unreal.StaticMeshComponent)
        if not component or not component.static_mesh:continue
        name=component.static_mesh.get_name().lower()
        if not actor.get_actor_label().startswith('SC_') or not any(k in name for k in ['floor','road','ground','platform','stair']):continue
        centre,extent=actor.get_actor_bounds(False)
        if max(extent.x,extent.y)>15000 or min(extent.x,extent.y)<15:continue
        key=actor.get_actor_label();rectangles[key]=[centre.x-extent.x,centre.y-extent.y,centre.x+extent.x,centre.y+extent.y]
assert rectangles
definition=unreal.load_asset('/Game/Extraction/Maps/DA_SpaceCenterGameplay');points=[]
for rectangle in rectangles.values():points.extend([unreal.Vector2D(*rectangle[:2]),unreal.Vector2D(*rectangle[2:])])
definition.set_editor_property('floor_bounds',points)
definition.set_editor_property('overview_min',unreal.Vector2D(min(p.x for p in points)-500,min(p.y for p in points)-500))
definition.set_editor_property('overview_max',unreal.Vector2D(max(p.x for p in points)+500,max(p.y for p in points)+500))
assert lib.save_loaded_asset(definition)
out=root/'Docs/Extraction/RestorationEvidence/UI';out.mkdir(parents=True,exist_ok=True)
(out/'MapProjection.json').write_text(json.dumps({'rectangles':len(rectangles),'basis':'world-space restored floor/road/stair actor bounds; +X north; +Y east',
    'limits':'Project overview, not official minimap artwork. Rectangles approximate floor footprints; vertical overlap remains visible.',
    'actors':rectangles},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('TACTICAL_MAP_CONFIGURED',len(rectangles))
