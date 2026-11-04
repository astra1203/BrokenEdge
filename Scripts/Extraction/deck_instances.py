"""Identify the 17 m elevated deck near the north-ridge spawn and list all instances of its meshes (#29)."""
import json

import unreal

LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
POINTS = [(-6500, 33300), (-8000, 31900), (-5000, 34500), (-9500, 30500), (-4200, 35500), (-10500, 29300)]
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(LEVEL)
world = unreal.EditorLevelLibrary.get_editor_world()
SYS = unreal.SystemLibrary
seen = {}
for x, y in POINTS:
    hit = SYS.line_trace_single(world, unreal.Vector(x, y, 2600), unreal.Vector(x, y, -3000),
                                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
    t = hit.to_tuple()
    comp = t[10]
    item = t[13] if len(t) > 13 else -1
    mesh = comp.static_mesh if isinstance(comp, unreal.StaticMeshComponent) else None
    print('DECK_HIT', x, y, round(t[5].z), t[9].get_actor_label(), comp.get_name(), mesh.get_name() if mesh else None, item)
    if mesh:
        seen[mesh.get_path_name()] = mesh
actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for path, mesh in seen.items():
    bb = mesh.get_bounding_box()
    rows = []
    for a in actors_sub.get_all_level_actors():
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            if c.static_mesh != mesh:
                continue
            xs = [c.get_instance_transform(i, True) for i in range(c.get_instance_count())] \
                if isinstance(c, unreal.InstancedStaticMeshComponent) else [c.get_world_transform()]
            for tr in xs:
                p, r = tr.translation, tr.rotation.rotator()
                rows.append([round(p.x), round(p.y), round(p.z), round(r.yaw, 1), round(r.pitch, 1), round(r.roll, 1),
                             [round(v, 2) for v in tr.scale3d.to_tuple()], a.get_actor_label()])
    print('DECK_MESH', mesh.get_name(), json.dumps([round(v) for v in bb.min.to_tuple()]),
          json.dumps([round(v) for v in bb.max.to_tuple()]), len(rows))
    for r in sorted(rows):
        print('DECK_INST', mesh.get_name(), json.dumps(r, ensure_ascii=False))
