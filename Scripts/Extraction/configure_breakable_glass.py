"""Replace ordinary window panes, retaining frames and each source instance transform."""
import hashlib
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir())
OUT = ROOT/'Saved/Extraction/Fix5767Glass'
OUT.mkdir(parents=True, exist_ok=True)
lib = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
builder = unreal.SovietMilitaryBuilder
active = json.loads((ROOT/'Scripts/SpaceCenter/restoration_active.json').read_text(encoding='utf-8'))
profile = json.loads((ROOT/active['profile_file']).read_text(encoding='utf-8'))
native = json.loads((ROOT/profile['work_directory']/'NativeAssets.json').read_text(encoding='utf-8'))
original = {v['asset'].split('.')[0]:k for k,v in native.items() if k.startswith('restoration_material:')}
cache = {}
report = (json.loads((OUT/'Report.json').read_text(encoding='utf-8')) if (OUT/'Report.json').exists()
          else {'maps': {}, 'meshes': {}, 'panes': []})

def digest(text): return hashlib.sha256(text.encode()).hexdigest()[:24]

def duplicate(source, name):
    dest = '/Game/Extraction/Glass/'+name
    if not lib.does_asset_exist(dest): assert lib.duplicate_asset(source, dest)
    return unreal.load_asset(dest)

def parts(mesh, indices):
    key = mesh.get_path_name()+str(indices)
    if key in cache: return cache[key]
    token = digest(key)
    glass = duplicate(mesh.get_path_name(), 'SM_Glass_'+token)
    assert builder.retain_mesh_sections(glass, indices), key
    assert lib.save_loaded_asset(glass)
    count = builder.mesh_island_count(glass)
    assert 0 < count <= 512, (key, count)
    panes = []
    for i in range(count):
        pane_path = '/Game/Extraction/Glass/SM_Pane_'+token+'_'+str(i)
        exists = lib.does_asset_exist(pane_path)
        pane = glass if count == 1 else duplicate(glass.get_path_name(), 'SM_Pane_'+token+'_'+str(i))
        if count > 1 and not exists: assert builder.retain_mesh_island(pane, i)
        assert lib.save_loaded_asset(pane)
        panes.append(pane)
    remaining = [i for i in range(len(mesh.get_editor_property('static_materials'))) if i not in indices]
    frame = None
    if remaining:
        candidate = duplicate(mesh.get_path_name(), 'SM_Frame_'+token)
        if builder.retain_mesh_sections(candidate, remaining):
            frame = candidate
            assert lib.save_loaded_asset(frame)
    cache[key] = frame, panes
    report['meshes'][key] = dict(frame=frame.get_path_name() if frame else None,
                               panes=[p.get_path_name() for p in panes])
    return frame, panes

def spawn_pane(key, mesh, materials, transform):
    token = digest(key)
    actor = actors.spawn_actor_from_class(unreal.ExtractionBreakableGlass, transform.translation, unreal.Rotator())
    actor.set_actor_transform(transform, False, True)
    actor.set_actor_label('BreakableGlass_'+token)
    actor.tags = ['ExtractionGlassV1']
    actor.set_folder_path('Extraction/BreakableGlass')
    pane_id, valid = unreal.GuidLibrary.parse_string_to_guid(hashlib.md5(key.encode()).hexdigest())
    assert valid
    actor.set_editor_property('pane_id', pane_id)
    actor.set_editor_property('replaces_legacy_window','/SovietMilitary/' in key)
    actor.set_editor_property('break_sound', unreal.load_asset('/Game/UVB80/Assets/UVB80/Environment/Sounds/SC_GlassBreak'))
    pane = actor.get_editor_property('pane')
    pane.set_static_mesh(mesh)
    for i,m in enumerate(materials):
        if m: pane.set_material(i,m)
    report['panes'].append(dict(key=key, id=hashlib.md5(key.encode()).hexdigest(), actor=actor.get_actor_label(),
                               mesh=mesh.get_path_name(), position=[transform.translation.x, transform.translation.y, transform.translation.z]))

for level in ['/Game/Maps/SovietMilitary/L_SovietMilitaryExtraction',
              '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction', '/Game/Maps/SpaceCenter/L_SpaceCenterNorth']:
    assert levels.load_level(level)
    assert levels.set_current_level_by_name(level.rsplit('/',1)[-1])
    before = len(report['panes'])
    owned = [a for a in actors.get_all_level_actors() if a.get_level().get_path_name().startswith(level+'.')]
    # Regeneration uses pristine map inputs. Refuse to duplicate or silently erase prior panes.
    if any(isinstance(a, unreal.ExtractionBreakableGlass) for a in owned):
        assert level in report['maps'], 'Existing glass has no provenance report: '+level
        continue
    for actor in owned:
        components = list(actor.get_components_by_class(unreal.StaticMeshComponent))
        soviet_window = 'SovietMilitary' in level and any(c.get_name().endswith('_StaticGlass') for c in components)
        if soviet_window:
            for component in components:
                mesh = component.static_mesh
                if not mesh: continue
                materials = [component.get_material(i) for i in range(component.get_num_materials())]
                transform = component.get_world_transform()
                if component.get_name().endswith('_StaticGlass'):
                    spawn_pane(actor.get_path_name()+'.'+component.get_name(), mesh, materials, transform)
                    component.set_visibility(False)
                    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            # Building construction scripts own the original child actors and frames.
            # The native pane suppresses its matching old glass again in BeginPlay.
            continue
        if 'SovietMilitary' in level: continue
        for component in components:
            mesh = component.static_mesh
            if not mesh: continue
            name = mesh.get_path_name().lower()
            if 'broken' in name or not any(s in name for s in ['building','window','interwall','exterwall']): continue
            materials = [component.get_material(i) for i in range(component.get_num_materials())]
            sources = [original.get(m.get_path_name().split('.')[0],m.get_path_name()) if m else '' for m in materials]
            indices = [i for i,m in enumerate(sources) if 'glass' in m.lower() and 'broken' not in m.lower()]
            if not indices: continue
            frame, panes = parts(mesh, indices)
            transforms = ([component.get_instance_transform(i, world_space=True) for i in range(component.get_instance_count())]
                          if isinstance(component, unreal.InstancedStaticMeshComponent) else [component.get_world_transform()])
            for i,transform in enumerate(transforms):
                for j,pane in enumerate(panes):
                    spawn_pane(actor.get_path_name()+'.'+component.get_name()+':'+str(i)+':'+str(j),pane,materials,transform)
            if frame: component.set_static_mesh(frame)
            else:
                component.set_static_mesh(None)
                component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    assert levels.save_current_level()
    report['maps'][level] = len(report['panes'])-before
    (OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('GLASS_MAP_COMPLETE',level,report['maps'][level],flush=True)
print('BREAKABLE_GLASS_COMPLETE',len(report['panes']))
