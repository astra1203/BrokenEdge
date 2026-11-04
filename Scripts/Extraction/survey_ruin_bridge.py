"""List every mesh near the launch-area ruined road (RoadFacility_Ground_L_17*_Ruin, ~(4672, 21538)) (#29)."""
import json
import math

import unreal

LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
CENTER = (4672, 21538, -176)
R = 4500
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level(LEVEL)
rows = []
for a in actors_sub.get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        mesh = c.static_mesh
        if isinstance(c, unreal.InstancedStaticMeshComponent):
            xs = [c.get_instance_transform(i, True) for i in range(c.get_instance_count())]
        else:
            xs = [c.get_world_transform()]
        for t in xs:
            p = t.translation
            if math.dist((p.x, p.y), CENTER[:2]) > R:
                continue
            b0, b1 = (mesh.get_bounding_box().min, mesh.get_bounding_box().max) if mesh else (None, None)
            rows.append([mesh.get_name() if mesh else None, a.get_actor_label(), [round(p.x), round(p.y), round(p.z)],
                         round(t.rotation.rotator().yaw), [round(v) for v in (b1 - b0).to_tuple()] if mesh else None])
rows.sort(key=lambda r: (r[0] or ''))
for r in rows:
    if r[0] and any(k in r[0] for k in ('Road', 'Bridge', 'Ruin', 'Ground', 'Rail', 'Guard', 'Pillar', 'Pier')) or not r[0]:
        print('RUIN_NEAR', json.dumps(r, ensure_ascii=False))
print('RUIN_TOTAL', len(rows))
