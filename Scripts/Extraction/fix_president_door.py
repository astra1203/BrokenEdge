"""38 号：总裁室会客室门洞实测约 290 cm 宽（双开门洞，probe_president_door.py 横穿墙扫描：开口在门中心 -75..+215），
原钥匙门只有 170 cm 单扇、偏在一侧，另一半空着，不刷卡也能进。改成居中、覆盖整个门洞的双开门。"""
import unreal
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
unreal.StingerAssetLibrary.finish_validation_asset_compilation()
world = unreal.EditorLevelLibrary.get_editor_world(); S = unreal.SystemLibrary
door = next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label() == 'KeyRoom_President')
p = door.get_actor_location()
OLD_Y = -2358.0
assert abs(p.y - OLD_Y) < 1 or abs(p.y - (OLD_Y + 70)) < 1, p
door.modify()
door.set_actor_location(unreal.Vector(p.x, OLD_Y + 70, p.z), False, False)
door.get_editor_property('barrier').set_box_extent(unreal.Vector(145, 15, 145))
door.set_editor_property('double_door', True)
door.set_editor_property('door_size', unreal.Vector(0, 0, 0))
door.rerun_construction_scripts() if hasattr(door, 'rerun_construction_scripts') else None
p = door.get_actor_location()
floor = p.z - 145
for dz in (40, 120, 200):
    cells = ''
    for dy in range(-300, 301, 10):
        a = unreal.Vector(p.x - 80, p.y + dy, floor + dz)
        h = S.line_trace_single(world, a, a + unreal.Vector(160, 0, 0), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
        cells += '#' if h else '.'
    unreal.log('PDOORFIX z+%d dy-300..+300 %s' % (dz, cells))
assert levels.save_current_level()
unreal.log('PDOORFIX saved at %s' % [round(v) for v in p.to_tuple()])
