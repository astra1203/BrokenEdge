"""Restore saved climb geometry and validate its original endpoints, without moving exits."""
import configparser
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import sys
import traceback
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(ROOT/'Scripts/Traversal'))
from source_ladders import point, read, sha, write_json, canonical

OUT = Path(os.environ['TRAVERSAL_EDITOR_OUT'])
data = read(ROOT/'Saved/Traversal/Sources.json')
assert data['schema'] == 2 and not data['unresolved']
assert sha(data['source_manifest']) == data['source_manifest_sha256']
rung_data = read(ROOT/'Saved/Traversal/Rungs.json')
MAIN = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
DEST = '/Game/Extraction/Traversal/SourceLadders'
OWNER = 'TraversalSourceV2'
BINDINGS_ONLY = os.environ.get('TRAVERSAL_BINDINGS_ONLY') == '1'
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary
state_path = ROOT/'Saved/Traversal/SourceCorrection/NativeAssets.json'
state = read(state_path) if state_path.exists() else {}
report = dict(result='running', source_manifest_sha256=data['source_manifest_sha256'], candidates=[], removed=[], geometry=[], blocked_queries=[], floor_queries=[],
              bindings_only=BINDINGS_ONLY,
              boundary='Source geometry and editor collision audit; rendered traversal and network tests remain separate.')


def vector(v): return unreal.Vector(*v)
def array(v): return [v.x, v.y, v.z]


def import_asset(entry, folder, cls):
    digest = sha(entry['file'])
    assert digest == entry['sha256']
    previous = state.get(entry['source'])
    if previous:
        assert previous['sha256'] == digest, 'Changed geometry needs a new explicit revision'
        obj = lib.load_asset(previous['asset'])
        assert isinstance(obj, cls)
        return obj
    task = unreal.AssetImportTask()
    task.filename = entry['file']; task.destination_path = folder
    task.automated = True; task.save = True; task.replace_existing = False
    assert not lib.does_directory_exist(folder), 'Untracked import folder: ' + folder
    tools.import_asset_tasks([task])
    assets = [lib.load_asset(p) for p in lib.list_assets(folder, recursive=True)]
    matches = [obj for obj in assets if isinstance(obj, cls)]
    assert len(matches) == 1, folder
    obj = matches[0]
    state[entry['source']] = dict(sha256=digest, asset=obj.get_path_name())
    write_json(state_path, state)
    return obj


def source_transform(m):
    # Current source instances are upright. Fail explicitly if this changes.
    scale = [math.sqrt(sum(m[i][j]**2 for i in range(3))) for j in range(3)]
    assert min(scale) > 0 and abs(m[2][0])+abs(m[2][1])+abs(m[0][2])+abs(m[1][2]) < .001
    yaw = math.degrees(math.atan2(m[1][0], m[0][0]))
    return unreal.Transform(location=vector(point(m)), rotation=unreal.Rotator(pitch=0, yaw=yaw, roll=0), scale=vector(scale))


def material_assets():
    textures = {}
    for path, entry in data['textures'].items():
        obj = import_asset(entry, DEST+'/Textures/'+path.rsplit('/', 1)[-1], unreal.Texture2D)
        obj.set_editor_property('srgb', entry['srgb'])
        obj.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_BC7)
        lib.save_loaded_asset(obj)
        textures[path] = obj
    active = read(ROOT/'Scripts/SpaceCenter/restoration_active.json')
    profile = read(ROOT/active['profile_file'])
    native = read(ROOT/profile['work_directory']/'NativeAssets.json')
    master_name = 'M_SC_R'+str(profile['material_revision'])+'_1_opaque'
    parent = lib.load_asset(native['restoration_master:'+master_name]['asset'])
    assert parent
    materials = {}
    for path, entry in data['materials'].items():
        asset_path = DEST+'/Materials/'+path.rsplit('/', 1)[-1]
        mi = lib.load_asset(asset_path) if lib.does_asset_exist(asset_path) else tools.create_asset(
            path.rsplit('/', 1)[-1], DEST+'/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(mi, parent)
        edit.set_material_instance_texture_parameter_value(mi, 'Base0', textures[entry['textures']['BaseColorMap']])
        edit.set_material_instance_texture_parameter_value(mi, 'Normal0', textures[entry['textures']['NRMap']])
        edit.set_material_instance_vector_parameter_value(mi, 'Layer0', unreal.LinearColor(3, 1, 1, 1))
        edit.update_material_instance(mi); lib.save_loaded_asset(mi)
        materials[path] = mi
    return materials


def clear(a, b):
    result = unreal.StingerAssetLibrary.traversal_capsule_clear(world, vector(a), vector(b), radius, half)
    if not result:
        hit = unreal.SystemLibrary.capsule_trace_single_by_profile(world, vector(a), vector(b), radius, half, 'Pawn', False, [], unreal.DrawDebugTrace.NONE, True)
        values = hit.to_tuple() if hit else None
        report['blocked_queries'].append(dict(start=a,end=b,actor=values[9].get_actor_label() if values and values[9] else None,
            impact=array(values[5]) if values else None,normal=array(values[7]) if values else None))
    return result


def floor_at(requested, below=90):
    # Snap vertically at the saved XY; never hunt for an alternate exit.
    hit = unreal.StingerAssetLibrary.trace_traversal_surface(world, vector(requested)+unreal.Vector(0,0,60), vector(requested)-unreal.Vector(0,0,below))
    report['floor_queries'].append(dict(requested=requested,hit=[array(v) for v in hit] if hit else None))
    if not hit or hit[1].z < .71:
        return None, 'no_source_endpoint_floor'
    foot = hit[0]
    for x,y in ((radius*.8,0),(-radius*.8,0),(0,radius*.8),(0,-radius*.8)):
        edge = unreal.StingerAssetLibrary.trace_traversal_surface(world, foot+unreal.Vector(x,y,12), foot+unreal.Vector(x,y,-12))
        if not edge or edge[1].z < .71 or abs(edge[0].z-foot.z)>8:
            return None, 'source_endpoint_footprint'
    # A line can hit below small rubble under the rounded capsule. Sweep down
    # at the same XY to obtain the actual standing centre on that surface.
    support = unreal.SystemLibrary.capsule_trace_single_by_profile(world, foot+unreal.Vector(0,0,half+30),
        foot+unreal.Vector(0,0,half-12), radius, half, 'Pawn', False, [], unreal.DrawDebugTrace.NONE, True)
    if support:
        values = support.to_tuple()
        if not values[1] and values[7].z >= .71:
            foot = values[4]-unreal.Vector(0,0,half)
    center = array(foot+unreal.Vector(0,0,half+3))
    if not clear(center, [center[0],center[1],center[2]+.1]):
        return None, 'source_endpoint_capsule'
    return array(foot), None


def path_clear(a, b, margin):
    height = max(a[2], b[2])+margin
    raised, across = [a[0],a[1],height], [b[0],b[1],height]
    return clear(a, raised) and clear(raised, across) and clear(across, b)


try:
    mapfile = ROOT/'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap'
    shutil.copy2(mapfile, OUT/'L_SpaceCenterExtraction.before.umap')
    report['map_sha256_before'] = sha(mapfile)
    if not BINDINGS_ONLY:
        assert levels.load_level(MAIN)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world and world.get_path_name().split('.')[0] == MAIN, 'Open the source map outside PIE before rebinding'
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Save existing map edits before rebinding'
    stream = unreal.GameplayStatics.get_streaming_level(world, '/Game/Maps/SpaceCenter/L_SpaceCenterNorth')
    if stream and stream.get_loaded_level():
        unreal.EditorLevelUtils.set_level_visibility(stream.get_loaded_level(), True, False)
    pawn_class = unreal.load_class(None, '/Game/Characters/Stinger/Blueprints/BP_StingerCharacter.BP_StingerCharacter_C')
    capsule = unreal.get_default_object(pawn_class).get_editor_property('capsule_component')
    radius, half = capsule.get_scaled_capsule_radius(), capsule.get_scaled_capsule_half_height()
    # Config-only UPROPERTY is not exposed to editor Python. Read the packaged
    # project setting explicitly and record it with the binding evidence.
    config = configparser.ConfigParser(strict=False, interpolation=None)
    config.read(ROOT/'Config/DefaultGame.ini', encoding='utf-8')
    roof_reach = config.getfloat('/Script/BrokenEdge.ExtractionTraversalRules', 'LadderRoofReach')
    assert roof_reach > 0
    report['game_config_sha256'] = sha(ROOT/'Config/DefaultGame.ini')
    report.update(radius=radius, half_height=half, roof_reach=roof_reach)
    materials = {} if BINDINGS_ONLY else material_assets()
    meshes = {}
    for path, entry in data['meshes'].items():
        if BINDINGS_ONLY:
            previous = state[entry['source']]
            assert previous['sha256'] == entry['sha256'] == sha(entry['file'])
            mesh = lib.load_asset(previous['asset'])
            assert isinstance(mesh, unreal.StaticMesh)
        else:
            mesh = import_asset(entry, DEST+'/Meshes/'+path.rsplit('/',1)[-1], unreal.StaticMesh)
        body = mesh.get_editor_property('body_setup')
        if BINDINGS_ONLY:
            assert body.get_editor_property('collision_trace_flag') == unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
            assert body.get_editor_property('double_sided_geometry')
        else:
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            body.set_editor_property('double_sided_geometry', True)
            for i, slot in enumerate(entry['source_mesh']['Properties']['StaticMaterials']):
                mesh.set_material(i, materials[canonical(slot['MaterialInterface']['ObjectPath'])])
            lib.save_loaded_asset(mesh)
        meshes[path] = mesh
        vertices = json.loads(unreal.StingerAssetLibrary.describe_ladder_vertices(mesh))
        bounds = entry['source_mesh']['Properties']['ExtendedBounds']
        assert vertices and max(abs(actual-(bounds['Origin'][axis]+sign*bounds['BoxExtent'][axis]))
            for sign in (-1,1) for i,axis in enumerate('XYZ')
            for actual in [(min if sign<0 else max)(p[i] for p in vertices)]) < 1., 'Native mesh differs from source bounds'
    existing = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    expected_geometry = set()
    for source in data['ladders']:
        for component in source['components']:
            label = 'Traversal_SourceMesh_'+source['id']+'_'+component['name']+'_'+str(component['instance'])
            expected_geometry.add(label)
            actor = existing.get(label)
            if actor:
                assert OWNER in [str(t) for t in actor.tags] and isinstance(actor, unreal.StaticMeshActor)
            else:
                assert not BINDINGS_ONLY, 'Missing source geometry: '+label
                actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector())
                actor.set_actor_label(label); actor.tags = [OWNER]
            expected = source_transform(component['transform'])
            if BINDINGS_ONLY:
                assert actor.static_mesh_component.static_mesh == meshes[component['mesh']], label
                assert str(actor.static_mesh_component.get_collision_profile_name()) == 'BlockAll', label
                actual = actor.get_actor_transform()
                for p in (unreal.Vector(), unreal.Vector(100,0,0), unreal.Vector(0,100,0), unreal.Vector(0,0,100)):
                    assert (unreal.MathLibrary.transform_location(actual,p)-unreal.MathLibrary.transform_location(expected,p)).length()<.1, label
            else:
                actor.static_mesh_component.set_static_mesh(meshes[component['mesh']])
                actor.static_mesh_component.set_collision_profile_name('BlockAll')
                actor.set_actor_transform(expected, False, True)
            report['geometry'].append(dict(label=label, source=component['source_reference'], instance=component['instance'],
                                           location=array(actor.get_actor_location())))
    for label, actor in existing.items():
        if OWNER in [str(t) for t in actor.tags] and isinstance(actor, unreal.StaticMeshActor) and label not in expected_geometry:
            assert not BINDINGS_ONLY, 'Unexpected source geometry: '+label
            actors.destroy_actor(actor); report['removed'].append(label)
    bindings = {a.get_editor_property('source_id'):a for a in actors.get_all_level_actors() if isinstance(a, unreal.ExtractionLadder)}
    for old_id in data['legacy_binding_ids']:
        old = bindings.pop(old_id, None)
        if old:
            assert old.get_actor_label() == 'Traversal_Ladder_'+old_id
            report['removed'].append(old.get_actor_label()); actors.destroy_actor(old)
    unreal.GameplayStatics.flush_level_streaming(world)
    if not BINDINGS_ONLY:
        unreal.SystemLibrary.execute_console_command(world, 'Editor.AsyncAssetCompilationFinishAll')
    # Asset imports/rebuilds invalidate editor physics bodies. Audit a freshly
    # loaded saved level, rather than querying the scene in the mutation frame.
    if not BINDINGS_ONLY:
        assert levels.save_current_level()
        assert levels.load_level('/Game/Maps/Lobby')
        assert levels.load_level(MAIN)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    stream = unreal.GameplayStatics.get_streaming_level(world, '/Game/Maps/SpaceCenter/L_SpaceCenterNorth')
    if stream and stream.get_loaded_level():
        unreal.EditorLevelUtils.set_level_visibility(stream.get_loaded_level(), True, False)
    unreal.GameplayStatics.flush_level_streaming(world)
    bindings = {a.get_editor_property('source_id'):a for a in actors.get_all_level_actors() if isinstance(a, unreal.ExtractionLadder)}
    for source in data['ladders']:
        endpoint = {name:point(value['transform']) for name,value in source['endpoints'].items()}
        row = dict(id=source['id'], source_object=source['source_object'], original_endpoints=endpoint)
        report['candidates'].append(row)
        facing_matrix = source['endpoints']['LadderBottomEndTransform']['transform']
        facing = vector([facing_matrix[i][0] for i in range(3)]).normal()
        assert abs(facing.z)<.001
        # Roof_Start is an animation anchor at the edge. Use the original top
        # interaction box's inner side, inset by the actual capsule radius.
        # This remains within the source trigger; no nearby platform is searched.
        trigger = source['triggers']['TopBox']; t = trigger['transform']
        extent = sum(abs(sum(t[i][j]*array(facing)[i] for i in range(3)))*trigger['extent'][j] for j in range(3))
        assert extent > radius+3
        top_stand = array(vector(point(t))+facing*(extent-radius-3))
        top_stand[2] = endpoint['LadderTopStartTransform'][2]
        row['capsule_top_stand_offset'] = array(vector(top_stand)-vector(endpoint['LadderTopStartTransform']))
        row['source_top_trigger'] = trigger
        bottom_exit, bottom_error = floor_at(endpoint['LadderBottomStartTransform'])
        top_exit, top_error = floor_at(top_stand, max(90, top_stand[2]-endpoint['LadderTopEndTransform'][2]))
        bottom = endpoint['LadderBottomEndTransform']; top_end = endpoint['LadderTopEndTransform']
        # Runtime subtracts LadderRoofReach from Top. Adapt that representation
        # to the original attachment point; this is not a source geometry offset.
        top = [top_end[0], top_end[1], top_end[2]+roof_reach]
        row.update(bottom_floor=bottom_exit, top_floor=top_exit, errors=[e for e in (bottom_error,top_error) if e])
        highest = -math.inf
        for component in source['components']:
            bounds = data['meshes'][component['mesh']]['source_mesh']['Properties']['ExtendedBounds']
            for x in (-1,1):
                for y in (-1,1):
                    for z in (-1,1):
                        local = [bounds['Origin'][k]+sign*bounds['BoxExtent'][k] for k,sign in zip('XYZ',(x,y,z))]
                        highest = max(highest, point(component['transform'],local)[2])
        # StartPath already adds 3/5 cm clearance above this source envelope.
        clearance = max(0.,highest-(top_exit or top_stand)[2])
        row['top_clearance'] = clearance
        if not row['errors']:
            centers = [[p[0],p[1],p[2]+half+3] for p in (bottom_exit,bottom,top_end,top_exit)]
            if not (path_clear(centers[0], centers[1],3) and clear(centers[1],centers[2])
                    and path_clear(centers[2],centers[3],5+clearance) and path_clear(centers[3],centers[2],3+clearance)):
                row['errors'].append('original_climb_or_exit_path_blocked')
        if row['errors']:
            row['classification'] = 'unresolved_collision'
            stale = bindings.get(source['id'])
            if stale and OWNER in [str(t) for t in stale.tags]:
                report['removed'].append(stale.get_actor_label()); actors.destroy_actor(stale)
            continue
        row['classification'] = 'usable'
        # Record standing positions outside each original trigger for real-input
        # acceptance. A missing approach is an explicit test gap, not permission
        # to move the source trigger or silently teleport into it.
        approaches, approach_errors = {}, {}
        for name, trigger_name, exit_point, direction in (
                # Unreal Python Vector.__neg__ mutates its operand. Multiplication
                # returns a new vector, keeping the bottom approach and actor yaw intact.
                ('bottom','BottomBox',bottom_exit,facing), ('top','TopBox',top_exit,facing * -1.)):
            trigger = source['triggers'][trigger_name]; t = trigger['transform']
            extent = sum(abs(sum(t[i][j]*array(direction)[i] for i in range(3)))*trigger['extent'][j] for j in range(3))
            p = array(vector(point(t))-direction*(extent+radius+20))
            p[2] = exit_point[2]
            ground, error = floor_at(p)
            if not error:
                a,b = [[v[0],v[1],v[2]+half+3] for v in (ground,exit_point)]
                if abs(ground[2]-exit_point[2])>45 or not path_clear(a,b,3): error='walking_approach_blocked'
            if error: approach_errors[name] = error
            else: approaches[name] = ground
        row.update(approaches=approaches, approach_errors=approach_errors)
        source_approach = vector(endpoint['LadderBottomEndTransform'])-vector(endpoint['LadderBottomStartTransform'])
        assert source_approach.x*facing.x+source_approach.y*facing.y > 0, 'Facing reversed relative to source entry: '+source['id']
        rotation = unreal.Rotator(pitch=0, yaw=math.degrees(math.atan2(facing.y, facing.x)), roll=0)
        actor = bindings.get(source['id'])
        if actor: assert OWNER in [str(t) for t in actor.tags]
        else: actor = actors.spawn_actor_from_class(unreal.ExtractionLadder, vector(bottom), rotation)
        actor.set_actor_label('Traversal_Ladder_'+source['id']); actor.tags = [OWNER]
        actor.set_editor_property('source_id', source['id'])
        actor.set_editor_property('top_clearance', clearance)
        actor.set_actor_location_and_rotation(vector(bottom), rotation, False, True)
        inverse = actor.get_actor_transform().inverse()
        entries = {}
        for field, name in (('bottom_entry','BottomBox'),('top_entry','TopBox')):
            trigger = source['triggers'][name]
            entry = unreal.TraversalLadderEntry()
            entry.set_editor_property('transform', unreal.MathLibrary.make_relative_transform(
                source_transform(trigger['transform']), actor.get_actor_transform()))
            entry.set_editor_property('extent', vector(trigger['extent']))
            actor.set_editor_property(field, entry)
            entries[field] = trigger
        points = dict(bottom=bottom, top=top, bottom_exit=bottom_exit, top_exit=top_exit)
        for name, value in points.items():
            actor.set_editor_property(name, unreal.MathLibrary.transform_location(inverse, vector(value)))
        rungs, widths = [], []
        for component in source['components']:
            geometry = rung_data[component['mesh']]
            assert geometry['source_sha256'] == data['meshes'][component['mesh']]['sha256']
            for rung in geometry['rungs']:
                p = point(component['transform'], [(rung['min'][0]+rung['max'][0])/2,rung['max'][1],rung['z']])
                rungs.append(p)
                left = vector(point(component['transform'], [rung['min'][0],rung['max'][1],rung['z']]))
                right = vector(point(component['transform'], [rung['max'][0],rung['max'][1],rung['z']]))
                widths.append((right-left).length()/2)
        rungs.sort(key=lambda p:p[2])
        assert len(rungs)>1 and min(widths)>6
        actor.set_editor_property('rungs', [unreal.MathLibrary.transform_location(inverse,vector(p)) for p in rungs])
        actor.set_editor_property('grip_origin', unreal.MathLibrary.transform_location(inverse,vector(rungs[0])))
        actor.set_editor_property('rung_spacing', statistics.median(b[2]-a[2] for a,b in zip(rungs,rungs[1:])))
        actor.set_editor_property('grip_half_width', min(widths)-6.)
        row.update(points=points, entries=entries, facing=array(facing), contact_rungs=rungs, grip_half_width=min(widths)-6., actor=actor.get_path_name())
    for source_id, actor in bindings.items():
        if source_id not in {s['id'] for s in data['ladders']} and OWNER in [str(t) for t in actor.tags]:
            report['removed'].append(actor.get_actor_label()); actors.destroy_actor(actor)
    assert levels.save_current_level()
    report['map_sha256_after'] = sha(mapfile)
    report['counts'] = {kind:sum(row['classification']==kind for row in report['candidates']) for kind in ('usable','unresolved_collision')}
    write_json(ROOT/'Scripts/Traversal/source_bindings.json', dict(
        schema=3, interaction='walk_toward_entry', source_manifest_sha256=data['source_manifest_sha256'], map_sha256=report['map_sha256_after'],
        editor_report=str(OUT/'Report.json'), candidates=report['candidates']))
    report['result'] = 'passed'  # Audit completed; unresolved rows are not accepted ladders.
except Exception:
    report.update(result='failed', error=traceback.format_exc())
    raise
finally:
    write_json(OUT/'Report.json', report)
