"""Survey bridge / rocket / launch meshes and empty (unrestored) mesh actors in the Space Center level (#29)."""
import json
from collections import defaultdict

import unreal

LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
KEYS = ('Bridge', 'bridge', 'Rocket', 'rocket', 'Launch', 'launch', 'Broken', 'broken', 'Collapse', 'Ruin', 'Damage')
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level(LEVEL)
groups = defaultdict(list)
empty = []
for a in actors_sub.get_all_level_actors():
    comps = a.get_components_by_class(unreal.StaticMeshComponent)
    for c in comps:
        mesh = c.static_mesh
        if mesh is None:
            empty.append([a.get_actor_label(), c.get_name(), [round(v) for v in c.get_world_location().to_tuple()]])
            continue
        name = mesh.get_name()
        if not any(k in name for k in KEYS):
            continue
        if isinstance(c, unreal.InstancedStaticMeshComponent):
            for i in range(c.get_instance_count()):
                t = c.get_instance_transform(i, True).translation
                groups[name].append([round(t.x), round(t.y), round(t.z)])
        else:
            t = c.get_world_location()
            groups[name].append([round(t.x), round(t.y), round(t.z)])
for name, locs in sorted(groups.items()):
    print('BRIDGE_SURVEY', name, len(locs), json.dumps(locs[:12]))
print('BRIDGE_EMPTY', len(empty), json.dumps(empty[:60], ensure_ascii=False))
