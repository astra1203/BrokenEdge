"""List elevated WalkwaySystem / Bridge pieces near the north-ridge (launch) spawn (#29)."""
import json
import math

import unreal

LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
CENTER, R, MINZ = (-6500, 33000), 9000, 900
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level(LEVEL)
rows = []
for a in actors_sub.get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        mesh = c.static_mesh
        if not mesh or not any(k in mesh.get_name() for k in ('Walkway', 'Bridge', 'Catwalk', 'Gantry', 'Truss')):
            continue
        xs = [c.get_instance_transform(i, True) for i in range(c.get_instance_count())] \
            if isinstance(c, unreal.InstancedStaticMeshComponent) else [c.get_world_transform()]
        bb = mesh.get_bounding_box()
        for tr in xs:
            p = tr.translation
            if math.dist((p.x, p.y), CENTER) > R or p.z + bb.max.z < MINZ:
                continue
            rows.append([mesh.get_name().replace('SM_H_WalkwaySystem_', 'W_').replace('SM_I_WalkwaySystem_', 'Wi_'),
                         round(p.x), round(p.y), round(p.z), round(tr.rotation.rotator().yaw),
                         [round(v) for v in (bb.max - bb.min).to_tuple()]])
rows.sort(key=lambda r: (r[0], r[1], r[2]))
from collections import Counter
cnt = Counter(r[0] for r in rows)
print('WALK_COUNTS', json.dumps(cnt.most_common(), ensure_ascii=False))
for r in rows:
    if 'Railing' in r[0] or 'Rail' in r[0]:
        continue
    print('WALK', json.dumps(r, ensure_ascii=False))
