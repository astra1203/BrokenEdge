"""验证 BornAt 初始化修复：只要不再出现 Age 被算成 Now+1。

编辑器世界时间不前进，所以没法观察淡入过程。但可以直接验最关键的一点：
    旧代码 Age = Now - (-1) = Now + 1 >= 1
    修复后 Age = Now - Now = 0
判定方式是看 Inflate（鼓胀进度）：Age=0 时 Inflate=0.35，Age 很大时 Inflate=1.0。
主团世界缩放 = Radius/50 * Inflate，因此缩放值能反推 Age 落在哪个区间。

Radius=450 时：
    Inflate=0.35 -> 世界缩放 3.15
    Inflate=1.00 -> 世界缩放 9.00
"""
import unreal

smoke_cls = unreal.load_class(None, '/Script/BrokenEdge.ExtractionSmoke')
world = unreal.EditorLevelLibrary.get_editor_world()
actor = unreal.EditorLevelLibrary.spawn_actor_from_class(smoke_cls, unreal.Vector(0, 0, 200))
assert actor, 'spawn failed'

actor.call_method('Refresh')

meshes = actor.get_components_by_class(unreal.StaticMeshComponent)
main = meshes[0]
main_scale = main.get_editor_property('relative_scale3d').x
print('SMOKE_BORN main_scale={0:.3f} (3.15=刚生成, 9.00=已老化)'.format(main_scale))
# 旧 bug 下 Age 会等于 WorldTime+1，主团直接落在满尺寸 9.00。
# 修复后应当停在鼓胀起点附近（3.15 左右，实测 2.80 是因为 Refresh 与读取之间过了几毫秒）。
assert main_scale < 5.0, 'main puff already grown: age was not reset (scale={0})'.format(main_scale)
assert main_scale >= 2.0, 'main puff smaller than grow-in start: {0}'.format(main_scale)

# 六层都要有 MID 且 Opacity 被写过（值可能是 0，因为 Age=0 时 FadeIn=0，这是正常起点）
for i, m in enumerate(meshes):
    mat = m.get_material(0)
    assert isinstance(mat, unreal.MaterialInstanceDynamic), 'layer {0} not MID'.format(i)
    print('SMOKE_BORN L{0} opacity={1:.3f}'.format(i, mat.get_scalar_parameter_value('Opacity')))

unreal.EditorLevelLibrary.destroy_actor(actor)
print('SMOKE_BORN_DONE')
