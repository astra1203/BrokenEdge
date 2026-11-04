"""Probe door clearance against the standing capsule (2026-11-04 user request #23).

For every actor whose label/mesh looks like a door or door frame, sweep the standing player capsule
horizontally through the opening (both thin-axis directions) and report the ones where the standing
capsule is blocked overhead while the crouched one passes, plus what the blocking component is.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/probe_door_clearance.py" -NullRHI -unattended -nosound
"""
import json

import unreal

LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
PAWN = '/Game/Characters/Stinger/Blueprints/BP_StingerCharacter'
KEYS = ('door', 'Door', 'DOOR', 'gate', 'Gate', 'Men_', 'men_')

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level(LEVEL)
world = unreal.EditorLevelLibrary.get_editor_world()

cls = unreal.load_class(None, PAWN + '.BP_StingerCharacter_C')
cdo = unreal.get_default_object(cls)
cap = cdo.get_editor_property('capsule_component')
radius, half = cap.get_scaled_capsule_radius(), cap.get_scaled_capsule_half_height()
move = cdo.get_editor_property('character_movement')
crouch_half = move.get_editor_property('crouched_half_height')
step = move.get_editor_property('max_step_height')
print('DOOR_PROBE_CAPSULE', json.dumps({'radius': radius, 'half': half, 'crouch_half': crouch_half, 'step': step}))

SYS = unreal.SystemLibrary
CH = unreal.TraceTypeQuery.TRACE_TYPE_QUERY1  # Visibility; refined below with object query


def floor_z(x, y, top):
    hit = SYS.line_trace_single(world, unreal.Vector(x, y, top), unreal.Vector(x, y, top - 600),
                                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE,
                                True)
    return hit.to_tuple()[5].z if hit is not None else None


def sweep(start, end, r, h):
    res = SYS.capsule_trace_single_by_profile(world, start, end, r, h, 'Pawn', False, [],
                                              unreal.DrawDebugTrace.NONE, True)
    return res


actors = actors_sub.get_all_level_actors()
cands = []
for a in actors:
    label = a.get_actor_label()
    names = [label]
    if isinstance(a, unreal.StaticMeshActor):
        sm = a.static_mesh_component.static_mesh
        if sm:
            names.append(sm.get_name())
    if any(k in n for n in names for k in KEYS):
        cands.append((a, ' | '.join(names)))
print('DOOR_PROBE_CANDIDATES', len(cands), 'of', len(actors), json.dumps(sorted(set(n for _, n in cands))[:80]))
report = []
for a, name in cands:
    origin, ext = a.get_actor_bounds(False)
    if ext.z < 80 or ext.z > 400:
        continue
    thin_x = ext.x < ext.y
    if max(ext.x, ext.y) < 40:
        continue
    fz = floor_z(origin.x, origin.y, origin.z + ext.z)
    if fz is None:
        continue
    d = unreal.Vector(1, 0, 0) if thin_x else unreal.Vector(0, 1, 0)
    span = (ext.x if thin_x else ext.y) + radius + 60
    def run(h, lift=6):
        c = unreal.Vector(origin.x, origin.y, fz + h + lift)
        return sweep(c - d * span, c + d * span, radius, h)
    # 低处（门槛/门扇）由矮胶囊判定，矮胶囊都过不去说明是关着的门，不算头顶遮挡。
    if run(40) is not None:
        continue
    row = {'door': name, 'at': [round(origin.x), round(origin.y), round(fz)], 'ext': [round(ext.x), round(ext.y), round(ext.z)]}
    for h in (92, 88, 84, 80, 70):
        hit = run(h)
        row[str(h)] = None if hit is None else [round(hit.to_tuple()[5].z - fz), str(hit.to_tuple()[9].get_actor_label()) if hit.to_tuple()[9] else '']
    report.append(row)
for r in report:
    print('DOOR_PROBE_ROW', json.dumps(r, ensure_ascii=False))
