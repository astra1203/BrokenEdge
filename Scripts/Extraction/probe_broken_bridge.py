"""43 号：断桥事件把一段高架桥面换成倾斜的坍塌桥板（同一模型 + BlockAll）。检查这些桥段模型自身有没有碰撞数据：
在远处摆一块同样倾斜的桥板，从上往下打射线，打不中说明坍塌桥板踩上去会掉下去。"""
import unreal
L = lambda s: unreal.log('BROKENPROBE ' + str(s))
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
broken = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.ExtractionBrokenBridge)][0]
meshes = {}
for span in broken.get_editor_property('spans'):
    for part in span.get_editor_property('parts'):
        actor = part.get_editor_property('actor')
        for c in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            if c.get_fname() == part.get_editor_property('component'):
                m = c.get_editor_property('static_mesh')
                t = c.get_instance_transform(part.get_editor_property('instance'), True)
                meshes[m.get_path_name()] = m
                L('span %s mesh=%s at=%s coll=%s desc=%s' % (span.get_editor_property('label'), m.get_name(), t.translation,
                    c.get_collision_enabled(), unreal.StingerAssetLibrary.describe_component_collision(c, t.translation + unreal.Vector(0, 0, 500), t.translation - unreal.Vector(0, 0, 500))))
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
for i, (p, m) in enumerate(meshes.items()):
    body = m.get_editor_property('body_setup')
    agg = body.get_editor_property('agg_geom')
    n = sum(len(agg.get_editor_property(k)) for k in ('box_elems', 'convex_elems', 'sphyl_elems', 'sphere_elems'))
    b = m.get_bounds()
    L('mesh %s simple=%d flag=%s ext=%s' % (m.get_name(), n, body.get_editor_property('collision_trace_flag'), b.box_extent))
    at = unreal.Vector(0, 0, 60000 + i * 5000)
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, at, unreal.Rotator(pitch=18))
    smc = a.get_component_by_class(unreal.StaticMeshComponent)
    smc.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    smc.set_static_mesh(m)
    smc.set_collision_profile_name('BlockAll')
    unreal.StingerAssetLibrary.finish_validation_asset_compilation()
    o, e, _ = unreal.SystemLibrary.get_component_bounds(smc)
    hits = 0; total = 0
    for fx in range(-4, 5):
        for fy in range(-4, 5):
            s = unreal.Vector(o.x + e.x * fx / 5, o.y + e.y * fy / 5, o.z + e.z + 200)
            r = unreal.SystemLibrary.line_trace_single(world, s, s - unreal.Vector(0, 0, 2 * e.z + 400), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
            total += 1
            if r: hits += 1
    L('mesh %s tilted slab trace hits %d/%d' % (m.get_name(), hits, total))
    a.destroy_actor()
