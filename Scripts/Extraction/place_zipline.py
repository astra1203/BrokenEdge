"""Place the buoyancy-room zipline (2026-11-04 user request #26, project-defined).

浮力室深水区（Y -4450..-6100，池底约 -720）北侧一条无柱子的通道：上端在西侧池沿 (X≈-2500) 立 4.5 米立柱，
下端在深池底东边。端点高度用射线实测；再用玩家胶囊沿骑乘路径扫一遍，被挡就换下一组候选点。
Re-running replaces the previous ExtractionZipline actors.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/place_zipline.py" -NullRHI -unattended -nosound
"""
import json

import unreal

LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
POST = 450.0
HANG = 125.0
RADIUS, HALF = 34.0, 86.0
CANDIDATES = [((-2500, -4550), (-925, -4550)), ((-2550, -4600), (-775, -4600)), ((-2500, -4650), (-1000, -4500)),
              ((-2550, -4550), (-625, -4550))]

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level(LEVEL)
world = unreal.EditorLevelLibrary.get_editor_world()
SYS = unreal.SystemLibrary
for actor in actors_sub.get_all_level_actors():
    if isinstance(actor, unreal.ExtractionZipline):
        actors_sub.destroy_actor(actor)


def ground(x, y, top=1000):
    hit = SYS.line_trace_single(world, unreal.Vector(x, y, top), unreal.Vector(x, y, -2000),
                                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
    return hit.to_tuple()[5].z if hit else None


def blocked(a, b):
    hit = SYS.capsule_trace_single_by_profile(world, a, b, RADIUS, HALF, 'Pawn', False, [],
                                              unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return None
    t = hit.to_tuple()
    return [round(v) for v in t[5].to_tuple()], t[9].get_actor_label() if t[9] else ''


report = []
chosen = None
for top_xy, bottom_xy in CANDIDATES:
    rim, floor = ground(*top_xy), ground(*bottom_xy)
    if rim is None or floor is None:
        report.append({'top': top_xy, 'bottom': bottom_xy, 'error': 'no ground'})
        continue
    top = unreal.Vector(top_xy[0], top_xy[1], rim + POST)
    bottom = unreal.Vector(bottom_xy[0], bottom_xy[1], floor + POST)
    # 骑乘路径（胶囊中心）：两端挂索点下方 HANG；起点再往路径内收 60 cm，避免扫到起点立柱旁的地面。
    a, b = top - unreal.Vector(0, 0, HANG), bottom - unreal.Vector(0, 0, HANG)
    d = (b - a).normal()
    hit = blocked(a + d * 60, b - d * 60)
    report.append({'top': [round(v) for v in top.to_tuple()], 'bottom': [round(v) for v in bottom.to_tuple()],
                   'rim': round(rim), 'floor': round(floor), 'blocked': hit})
    if hit is None:
        chosen = (top, bottom)
        break
assert chosen, report
top, bottom = chosen
line = actors_sub.spawn_actor_from_class(unreal.ExtractionZipline, unreal.Vector(top.x, top.y, top.z - POST),
                                         unreal.Rotator())
line.set_actor_label('ME_BuoyancyPoolZipline')
line.set_editor_property('post_height', POST)
line.set_editor_property('top_anchor', unreal.Vector(0, 0, POST))
line.set_editor_property('bottom_anchor', bottom - line.get_actor_location())
line.set_editor_property('top_label', '池边')
line.set_editor_property('bottom_label', '池底')
line.rerun_construction_scripts() if hasattr(line, 'rerun_construction_scripts') else None
assert levels.save_current_level()
print('ZIPLINE_PLACED', json.dumps({'chosen': [[round(v) for v in top.to_tuple()], [round(v) for v in bottom.to_tuple()]],
                                    'tries': report}, ensure_ascii=False))
