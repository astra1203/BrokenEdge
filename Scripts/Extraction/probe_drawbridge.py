"""Measure the east drawbridge decks (lowered pose) against the road on both banks (2026-11-04 #28).

Prints, in the bridge's Others-local frame (X across the road, Y along the span, Z up):
deck bounds, the deck top surface height near both ends, and the bank surface heights just past each end.
"""
import json

import unreal

LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level(LEVEL)
world = unreal.EditorLevelLibrary.get_editor_world()
bridge = [a for a in actors_sub.get_all_level_actors() if isinstance(a, unreal.ExtractionDrawbridge)][0]
others = bridge.get_editor_property('others')
xf = others.get_world_transform()
SYS = unreal.SystemLibrary


def down(local_x, local_y, top=1500, ignore=()):
    start = xf.transform_location(unreal.Vector(local_x, local_y, top))
    end = xf.transform_location(unreal.Vector(local_x, local_y, -3000))
    hit = SYS.line_trace_single(world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, list(ignore),
                                unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return None
    t = hit.to_tuple()
    p = xf.inverse_transform_location(t[5])
    return [round(p.z, 1), t[9].get_actor_label() if t[9] else '', str(t[10].get_name()) if t[10] else '']


report = {'actor': [round(v) for v in bridge.get_actor_location().to_tuple()], 'yaw': bridge.get_actor_rotation().yaw}
for name in ('deck1', 'deck2'):
    deck = bridge.get_editor_property(name)
    hinge = deck.get_attach_parent()
    mesh = deck.static_mesh
    bmin, bmax = deck.get_local_bounds()
    corners = [xf.inverse_transform_location(deck.get_world_transform().transform_location(unreal.Vector(x, y, z)))
               for x in (bmin.x, bmax.x) for y in (bmin.y, bmax.y) for z in (bmin.z, bmax.z)]
    lo = [round(min(getattr(c, a) for c in corners)) for a in 'xyz']
    hi = [round(max(getattr(c, a) for c in corners)) for a in 'xyz']
    report[name] = {'mesh': mesh.get_name() if mesh else None, 'hinge': [round(v, 1) for v in hinge.get_editor_property('relative_location').to_tuple()],
                    'deck_rel': [round(v, 1) for v in deck.get_editor_property('relative_location').to_tuple()],
                    'deck_rot': str(hinge.get_editor_property('relative_rotation')), 'lo': lo, 'hi': hi}
# Sample along the span centre line, every 100 cm, covering both banks.
ys = sorted(set([report['deck1']['lo'][1], report['deck1']['hi'][1], report['deck2']['lo'][1], report['deck2']['hi'][1]]))
samples = []
x = (report['deck1']['lo'][0] + report['deck1']['hi'][0]) / 2
for y in range(int(ys[0]) - 800, int(ys[-1]) + 800, 100):
    samples.append([y, down(x, y)])
report['centre_x'] = x
report['samples'] = samples
for side in (-1, 1):
    report['edge_%d' % side] = [[y, down(x + side * 250, y)] for y in range(int(ys[0]) - 400, int(ys[-1]) + 400, 200)]
print('DRAWBRIDGE_PROBE', json.dumps(report, ensure_ascii=False))
