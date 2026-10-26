"""出生点诊断：在编辑器里复现 C++ ChoosePlayerStart 的校验，逐个点告诉你为什么不可站立。

为什么需要它：`RestartPlayerAtPlayerStart: Player start not found` 是 GameModeBase 打的，
它不会说 ChoosePlayerStart 内部是哪一步返回了 nullptr。这个脚本把每一步拆开打印，
不用编译、不用开 PIE。

运行（编辑器 Output Log，Cmd 模式）：
    py "D:/Users/SCSJ/BrokenEdge2.0/Scripts/Extraction/diagnose_spawn_groups.py"

输出每一行的含义：
    OLD-MISS / NEW-HIT   从点本身起探是空的，从点上方 292cm 起探命中
                         ⇒ 数据里的 Z 就贴在地面上，旧代码共面漏检（已修）
    NO-FLOOR             两种起探方式都探不到地面 ⇒ 该 XY 真的没有可站碰撞
    NORMAL-<n>           打到斜面/侧墙
    DROP-<n>             地面与 authored Z 差得太多（超出 ±窗口）
    CAPSULE-BLOCKED      有地面，但胶囊被挡住（贴墙/狭窄）
    OK                   这一点和 C++ 现在给的判定一致：可站立
"""
import collections

import unreal

ASSET = '/Game/Extraction/Maps/DA_SpaceCenterGameplay'
HALF, RADIUS = 92.0, 34.0
PROBE_UP = HALF + 200.0
PROBE_DOWN = HALF * 2.0 + 700.0
MAX_DROP = HALF + 600.0
NORMAL_MIN = 0.7


def probe(world, x, y, from_z, to_z):
    hit = unreal.StingerAssetLibrary.trace_traversal_surface(
        world, unreal.Vector(x, y, from_z), unreal.Vector(x, y, to_z))
    if not hit or len(hit) != 2:
        return None
    return hit[0], hit[1]


def main():
    asset = unreal.load_asset(ASSET)
    assert asset, ASSET
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    groups = asset.get_editor_property('spawn_groups')
    print('SPAWN_DIAG asset=%s world=%s groups=%d' % (ASSET, world.get_path_name(), len(groups)))

    summary = collections.Counter()
    for group in groups:
        gid = str(group.get_editor_property('id'))
        points = group.get_editor_property('positions')
        print('GROUP %-22s points=%d status=%s' % (gid, len(points),
              group.get_editor_property('reference_status')))
        states = []
        for index, transform in enumerate(points):
            c = transform.translation
            old = probe(world, c.x, c.y, c.z, c.z - (HALF + 500.0))
            new = probe(world, c.x, c.y, c.z + PROBE_UP, c.z + PROBE_UP - PROBE_DOWN)
            state = 'OK'
            if not new:
                state = 'OLD-MISS' if not old else 'NO-FLOOR'
            elif new[1].z < NORMAL_MIN:
                state = 'NORMAL-%.2f' % new[1].z
            elif not (-PROBE_UP <= c.z - new[0].z <= MAX_DROP):
                state = 'DROP-%.0f' % (c.z - new[0].z)
            else:
                center = new[0] + unreal.Vector(0.0, 0.0, HALF + 3.0)
                if not unreal.StingerAssetLibrary.traversal_capsule_clear(
                        world, center, center + unreal.Vector(0.0, 0.0, 0.01), RADIUS, HALF):
                    state = 'CAPSULE-BLOCKED'
            # 顺带记下旧起点（贴面）与新起点（上方 292cm）的差异来源
            if old and new and abs(old[0].z - new[0].z) > 1.0:
                state += ' (oldZ=%.0f newZ=%.0f)' % (old[0].z, new[0].z)
            states.append(state)
            summary[state.split(' ')[0]] += 1
            print('  [%d] authored=(%.0f, %.0f, %.0f) -> %s' % (index, c.x, c.y, c.z, state))
        ok = sum(1 for s in states if s == 'OK')
        print('  GROUP_RESULT %-22s ok=%d/%d valid_for_cpp=%s' % (
            gid, ok, len(states), 'YES' if ok == len(states) and len(states) >= 3 else 'NO'))

    print('SPAWN_DIAG_SUMMARY %s' % dict(summary))
    print('SPAWN_DIAG_NOTE 旧代码从点本身起探（OLD-MISS 就是被它判死的）；'
          '新代码从点上方 292cm 起探，并同时试 Pawn/Visibility 两个通道。')


main()
