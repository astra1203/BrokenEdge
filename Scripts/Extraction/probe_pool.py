"""浮力室池子测量（40 号）：浅区/深区/深坑的高度网格，深坑往下追到 -20000。"""
import unreal
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
world = unreal.EditorLevelLibrary.get_editor_world()
SYS = unreal.SystemLibrary
def ground(x, y, top=-200, bottom=-20000):
    hit = SYS.line_trace_single(world, unreal.Vector(x, y, top), unreal.Vector(x, y, bottom),
                                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
    if not hit: return None
    t = hit.to_tuple(); return t[5].z, (t[9].get_actor_label() if t[9] else '')
for x in range(-800, -2200, -100):
    line=[]
    for y in range(-6200, -4900, 100):
        g=ground(x,y,-650)
        line.append('  ---' if g is None else '%6d'%round(g[0]))
    unreal.log('PIT %6d|'%x+''.join(line))
g=ground(-1450,-5550,-650); unreal.log('PITCENTER %s'%(g,))
