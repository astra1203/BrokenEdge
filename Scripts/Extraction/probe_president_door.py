"""38 号：总裁室会客室钥匙门的现状与真实门洞位置（附近带 Door 的网格、水平射线找开口）。"""
import math
import unreal
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
world = unreal.EditorLevelLibrary.get_editor_world(); S = unreal.SystemLibrary
L = lambda s: unreal.log('PDOOR ' + s)
door = None
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_actor_label() == 'KeyRoom_President': door = a
p = door.get_actor_location(); r = door.get_actor_rotation()
L('door at %s yaw=%.1f barrier=%s doorsize=%s offset=%s slide=%s mesh=%s' % ([round(v) for v in p.to_tuple()], r.yaw,
  door.get_editor_property('barrier').get_unscaled_box_extent(), door.get_editor_property('door_size'), door.get_editor_property('door_mesh_offset'),
  door.get_editor_property('slide'), door.get_editor_property('door_mesh').get_editor_property('static_mesh')))
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        m = c.get_editor_property('static_mesh')
        if not m or 'oor' not in m.get_name(): continue
        o, e, _ = unreal.SystemLibrary.get_component_bounds(c)
        if abs(o.x - p.x) < 2500 and abs(o.y - p.y) < 2500 and abs(o.z - p.z) < 500:
            if isinstance(c, unreal.InstancedStaticMeshComponent):
                for i in range(c.get_instance_count()):
                    t = c.get_instance_transform(i, True)
                    q = t.translation
                    if abs(q.x - p.x) < 2500 and abs(q.y - p.y) < 2500 and abs(q.z - p.z) < 500:
                        L('mesh %s inst at %s yaw=%.0f coll=%s' % (m.get_name(), [round(v) for v in q.to_tuple()], t.rotation.rotator().yaw, c.get_collision_enabled()))
            else:
                L('mesh %s comp at %s ext=%s coll=%s' % (m.get_name(), [round(v) for v in o.to_tuple()], [round(v) for v in e.to_tuple()], c.get_collision_enabled()))
# 水平射线：从会客室锚点 (6121,-748, 2604+100) 每 10 度
base = unreal.Vector(6121, -748, 2704)
out = []
for d in range(0, 360, 10):
    v = unreal.Vector(math.cos(math.radians(d)), math.sin(math.radians(d)), 0)
    h = S.line_trace_single(world, base, base + v * 3000, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
    out.append('%d:%s' % (d, round((h.to_tuple()[5] - base).length()) if h else 'far'))
L('rays ' + ' '.join(out))
# 沿门所在墙（门扇平面沿世界 Y，墙法线沿 X）逐 10 cm 横穿墙打射线，忽略钥匙门自身，开口处射线畅通。
floor = p.z - 145
for dz in (40, 120, 200, 260):
    cells = ''
    for dy in range(-500, 501, 10):
        a = unreal.Vector(p.x - 80, p.y + dy, floor + dz)
        h = S.line_trace_single(world, a, a + unreal.Vector(160, 0, 0), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [door], unreal.DrawDebugTrace.NONE, True)
        cells += '#' if h else '.'
    L('wall z+%d y%+d..%+d: %s' % (dz, -500, 500, cells))
