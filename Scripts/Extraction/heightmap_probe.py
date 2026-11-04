"""Print a coarse height map (first hit downward from Z=2000) around a point, for locating map gaps (#29).

Edit CX, CY, HALF, STEP below. Each row is one Y line; '.' = no hit down to -3000, values are Z/100 (m).
"""
import unreal

CX, CY, HALF, STEP, TOP = -1025, -5700, 2000, 150, 1100
LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(LEVEL)
world = unreal.EditorLevelLibrary.get_editor_world()
SYS = unreal.SystemLibrary
print('HMAP_HEADER x from %d step %d; rows y descending' % (CX - HALF, STEP))
y = CY + HALF
while y >= CY - HALF:
    cells = []
    x = CX - HALF
    while x <= CX + HALF:
        hit = SYS.line_trace_single(world, unreal.Vector(x, y, TOP), unreal.Vector(x, y, -3000),
                                    unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
        cells.append('  .' if hit is None else '%3d' % round(hit.to_tuple()[5].z / 100))
        x += STEP
    print('HMAP %6d %s' % (y, ''.join(cells)))
    y -= STEP
