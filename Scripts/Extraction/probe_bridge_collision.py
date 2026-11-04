"""41 号：中控桥（中空桥）桥面一带，列出可见但不挡角色的静态网格（台阶踩上去会掉下去）。"""
import unreal
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
X0, X1, Y0, Y1, Z0, Z1 = 3800, 6200, -19000, -12800, 1100, 1900
L = lambda s: unreal.log('BRIDGECOL ' + s)
for a in actors:
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        if not c.get_editor_property('static_mesh'): continue
        o, e, _ = unreal.SystemLibrary.get_component_bounds(c)
        if o.x + e.x < X0 or o.x - e.x > X1 or o.y + e.y < Y0 or o.y - e.y > Y1 or o.z + e.z < Z0 or o.z - e.z > Z1: continue
        ce = c.get_collision_enabled()
        resp = c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)
        mesh = c.get_editor_property('static_mesh')
        body = mesh.get_editor_property('body_setup') if mesh else None
        simple = None
        try:
            simple = len(body.get_editor_property('agg_geom').get_editor_property('convex_elems')) + len(body.get_editor_property('agg_geom').get_editor_property('box_elems'))
            flag = body.get_editor_property('collision_trace_flag')
        except Exception as ex:
            flag = '?'
        bad = ce == unreal.CollisionEnabled.NO_COLLISION or resp != unreal.CollisionResponseType.ECR_BLOCK or (simple == 0 and str(flag).find('SIMPLE_AND_COMPLEX') < 0 and str(flag).find('COMPLEX_AS_SIMPLE') < 0)
        if bad and e.x * e.y > 400:
            L('%s | %s | %s | top=%.0f ext=(%.0f,%.0f,%.0f) at=(%.0f,%.0f) coll=%s pawn=%s simple=%s flag=%s' % (
                a.get_actor_label(), c.get_name(), mesh.get_name(), o.z + e.z, e.x, e.y, e.z, o.x, o.y, ce, resp, simple, flag))
