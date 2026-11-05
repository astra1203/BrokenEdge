"""Read-only editor survey for fixes 57-67; output stays in Saved."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir())
OUT = ROOT / 'Saved/Extraction/Fix5767Survey'
OUT.mkdir(parents=True, exist_ok=True)
lib = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def vector(v): return [v.x, v.y, v.z]

report = {'maps': {}, 'weapons': {}, 'items': []}
active = json.loads((ROOT/'Scripts/SpaceCenter/restoration_active.json').read_text(encoding='utf-8'))
profile = json.loads((ROOT/active['profile_file']).read_text(encoding='utf-8'))
native = json.loads((ROOT/profile['work_directory']/'NativeAssets.json').read_text(encoding='utf-8'))
original = {v['asset'].split('.')[0]: k for k,v in native.items() if k.startswith('restoration_material:')}
for level in ['/Game/Maps/SovietMilitary/L_SovietMilitaryExtraction',
              '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction', '/Game/Maps/SpaceCenter/L_SpaceCenterNorth']:
    assert levels.load_level(level), level
    rows = []
    count = 0
    for actor in actors.get_all_level_actors():
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = component.static_mesh
            if not mesh: continue
            mats = [component.get_material(i).get_path_name() if component.get_material(i) else ''
                    for i in range(component.get_num_materials())]
            sources = [original.get(m.split('.')[0],m) for m in mats]
            indices = [i for i, m in enumerate(sources) if 'glass' in m.lower() and 'optic' not in m.lower()]
            if not indices: continue
            count += 1
            origin, extent = actor.get_actor_bounds(False)
            rows.append(dict(actor=actor.get_path_name(), label=actor.get_actor_label(),
                             component=component.get_name(), cls=component.get_class().get_name(),
                             mesh=mesh.get_path_name(), materials=mats, sources=sources, glass_indices=indices,
                             location=vector(component.get_world_location()),
                             rotation=str(component.get_world_rotation()), scale=vector(component.get_world_scale()),
                             bounds=[vector(origin), vector(extent)],
                             instances=component.get_instance_count() if isinstance(component, unreal.InstancedStaticMeshComponent) else 1))
    report['maps'][level] = rows
    print('GLASS_SURVEY', level, count)
for gun in ['BoltSniper', 'PumpShotgun']:
    da = unreal.load_asset('/Game/Extraction/Weapons/' + gun + '/DA_' + gun + '_Presentation')
    fit = da.get_editor_property('view_fit')
    bp = unreal.load_asset('/Game/Extraction/Weapons/' + gun + '/BP_' + gun)
    cdo = unreal.get_default_object(bp.generated_class())
    report['weapons'][gun] = dict(hip=str(fit.get_editor_property('hip')), aim=str(fit.get_editor_property('aim')),
                                 hand=str(da.get_editor_property('world_hand_transform')),
                                 crosshairs={k: str(cdo.get_editor_property('crosshairs_' + k))
                                             for k in ['center', 'left', 'right', 'top', 'bottom']})
for path in lib.list_assets('/Game/Extraction/Items', recursive=False, include_folder=False):
    d = unreal.load_asset(path)
    if isinstance(d, unreal.ExtractionItemDefinition):
        mesh = d.get_editor_property('world_mesh')
        report['items'].append(dict(id=str(d.get_editor_property('item_id')),
                                   kind=str(d.get_editor_property('kind')),
                                   mesh=mesh.get_path_name() if mesh else None))
(OUT/'Survey.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('FIX5767_SURVEY_COMPLETE', len(report['items']))
