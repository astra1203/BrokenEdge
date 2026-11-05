"""43 号：沿中控桥做碰撞剖面：网格逐点从上往下打射线，记录命中高度和命中的组件，找桥面上的洞/台阶。"""
import unreal, json
L = lambda s: unreal.log('BPROF ' + str(s))
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
import os
X0, X1, Y0, Y1, STEP = [int(v) for v in os.environ.get('BPROF_BOX', '4300,5500,-19600,-11400,25').split(',')]
rows = []
names = {}
for yi in range(int((Y1 - Y0) / STEP) + 1):
    y = Y0 + yi * STEP
    row = []
    for xi in range(int((X1 - X0) / STEP) + 1):
        x = X0 + xi * STEP
        r = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 3500), unreal.Vector(x, y, -2000),
                                                   unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
        if r:
            t = r.to_tuple()
            comp = t[10]
            key = comp.get_editor_property('static_mesh').get_name() if isinstance(comp, unreal.StaticMeshComponent) and comp.get_editor_property('static_mesh') else '?'
            names.setdefault(key, len(names))
            row.append([round(t[4].z), names[key]])
        else:
            row.append([-99999, -1])
    rows.append(row)
out = unreal.Paths.project_saved_dir() + 'Extraction/bridge_profile_%d_%d.json' % (X0, Y0)
json.dump({'x0': X0, 'y0': Y0, 'step': STEP, 'rows': rows, 'names': {v: k for k, v in names.items()}}, open(out, 'w'))
L('written %s %d rows' % (out, len(rows)))
