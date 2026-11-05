"""Read-only geometry survey for the Soviet Military integration."""
import unreal, json
from pathlib import Path

world = unreal.EditorLoadingAndSavingUtils.load_map('/Game/UVB80/Levels/UVB80')
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
def vec(v): return [v.x, v.y, v.z]
rows = []
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    o, e = a.get_actor_bounds(False)
    row = dict(name=a.get_name(), label=a.get_actor_label(), cls=a.get_class().get_name(),
               location=vec(a.get_actor_location()), bounds=[vec(o), vec(e)], meshes=[])
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        if c.static_mesh:
            row['meshes'].append(dict(name=c.get_name(), mesh=c.static_mesh.get_path_name(),
                location=vec(c.get_world_location()), rotation=str(c.get_world_rotation()),
                scale=vec(c.get_world_scale()), materials=[str(c.get_material(i).get_path_name()) if c.get_material(i) else '' for i in range(c.get_num_materials())]))
    rows.append(row)
out=Path(unreal.Paths.project_saved_dir())/'Extraction/SovietMilitary'
out.mkdir(parents=True,exist_ok=True)
(out/'Survey.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('SOVIET_SURVEY_COMPLETE %d'%len(rows))
