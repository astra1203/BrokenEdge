"""43 号：中控桥逐列多层射线：每次命中后从命中点下方继续往下打，列出这一列所有可站表面（高度/组件/网格）。"""
import unreal, json
L = lambda s: unreal.log('BLAY ' + str(s))
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
out = {}
for x in (4500, 4650, 4800, 4881, 4960, 5100, 5250):
    col = []
    for y in range(-19000, -12000, 50):
        z = 3500; layers = []
        for _ in range(14):
            r = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, z), unreal.Vector(x, y, -1500),
                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
            if not r: break
            t = r.to_tuple()
            if t[1]:  # 起点在实体里面（简单盒子碰撞）：往下跳一段再打
                z -= 40
                continue
            c = t[10]
            m = c.get_editor_property('static_mesh').get_name() if isinstance(c, unreal.StaticMeshComponent) and c.get_editor_property('static_mesh') else (c.get_name() if c else '?')
            layers.append([round(t[4].z), m, round(t[7].z, 2)])
            z = t[4].z - 3
        col.append([y, layers])
    out[x] = col
p = unreal.Paths.project_saved_dir() + 'Extraction/bridge_layers.json'
json.dump(out, open(p, 'w'), ensure_ascii=False)
L('written ' + p)
