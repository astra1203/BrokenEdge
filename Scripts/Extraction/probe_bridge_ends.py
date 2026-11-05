"""43 号：中控桥的通行桥面在 z≈907，两端桥面之外碰撞直接掉到地面（≈50）。列出两端一带可见但不挡角色的网格（上下桥的台阶）。"""
import unreal
L = lambda s: unreal.log('BENDS ' + s)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
REGIONS = [(3600, 6200, -21500, -18500), (3600, 6200, -12900, -10000)]
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        mesh = c.get_editor_property('static_mesh')
        if not mesh: continue
        o, e, _ = unreal.SystemLibrary.get_component_bounds(c)
        if o.z + e.z < 80 or o.z - e.z > 1000: continue
        if not any(o.x + e.x >= x0 and o.x - e.x <= x1 and o.y + e.y >= y0 and o.y - e.y <= y1 for x0, x1, y0, y1 in REGIONS): continue
        if e.x * e.y < 2500: continue
        desc = unreal.StingerAssetLibrary.describe_component_collision(c, unreal.Vector(o.x, o.y, o.z + e.z + 300), unreal.Vector(o.x, o.y, o.z - e.z - 100))
        n = 1
        if isinstance(c, unreal.InstancedStaticMeshComponent): n = c.get_instance_count()
        L('%s | %s | %s | z %.0f..%.0f ext=(%.0f,%.0f) at=(%.0f,%.0f) n=%d | %s' % (a.get_actor_label(), c.get_name(), mesh.get_name(),
            o.z - e.z, o.z + e.z, e.x, e.y, o.x, o.y, n, desc))
