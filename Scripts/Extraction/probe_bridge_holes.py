"""41 号：中控桥（中空桥）桥面逐点（50 cm）往下打射线，列出桥面范围内射线落到桥面以下的洞，以及打到的桥体部件。
先等网格异步编译完，否则命令行里的物理体还没建好，射线不可信。"""
import unreal
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
world = unreal.EditorLevelLibrary.get_editor_world(); S = unreal.SystemLibrary
holes = []; parts = {}
rows = []
for x in range(4450, 5351, 50):
    line = ''
    for y in range(-18700, -12599, 100):
        h = S.line_trace_single_by_profile(world, unreal.Vector(x, y, 2600), unreal.Vector(x, y, 0), 'Pawn', False, [], unreal.DrawDebugTrace.NONE, True)
        if not h:
            line += ' '; continue
        t = h.to_tuple(); z = t[5].z; name = t[9].get_actor_label() if t[9] else ''
        parts[name] = parts.get(name, 0) + 1
        line += '#' if z > 1600 else ('=' if z > 1400 else ('.' if z > 600 else 'v'))
    rows.append('%5d %s' % (x, line))
unreal.log('HOLES legend: # >1600  = deck 1400-1600  . 600-1400  v below 600 (falls to ground)  blank none; y from -18700 step 100')
for r in rows: unreal.log('HOLES ' + r)
unreal.log('HOLES parts %s' % sorted(parts.items(), key=lambda kv: -kv[1])[:12])
