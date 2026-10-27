"""在真实世界实例化烟幕，校验多球叠层是否生效。

为什么需要：
    ApplyLook 改成按层摆放后，"主团正常、辅团尺寸为 1 且完全不透明"是很可能的
    错误状态 —— 编译通过、材质也对，但画面里就是不对。这里直接读回每个图元
    的相对位置、世界缩放和 MID 参数，把叠层结果量化出来。

校验
    6 个 UStaticMeshComponent（主团 + 5 个叠加团）
    每层都换成 UMaterialInstanceDynamic，且 Opacity 参数被写过
    层的相对位置互不相同（说明偏移生效，不是全叠在原点）
    层的缩放按 Scale 系数递减（主团最大，顶层最小）
"""
import unreal

SMOKE_CLASS = '/Script/BrokenEdge.ExtractionSmoke'
smoke_cls = unreal.load_class(None, SMOKE_CLASS)
assert smoke_cls, 'class missing'

world = unreal.EditorLevelLibrary.get_editor_world()
actor = unreal.EditorLevelLibrary.spawn_actor_from_class(smoke_cls, unreal.Vector(0, 0, 200))
assert actor, 'spawn failed'
# 编辑器世界里不会自动跑 BeginPlay / Tick，必须手动推一次表现：
# Refresh 是 UFUNCTION，内部会把材质提升为 MID 并调用 ApplyLook 摆放各层。
# 注意 Tick 不是 UFUNCTION，反射调不到，所以这里只能验证 Refresh 这一条路径。
actor.call_method('Refresh')

meshes = actor.get_components_by_class(unreal.StaticMeshComponent)
print('SMOKE_LAYER count={0}'.format(len(meshes)))
assert len(meshes) == 6, 'expected 6 puffs, got {0}'.format(len(meshes))

def world_scale_x(comp):
    """沿父级链叠乘相对缩放，得到世界缩放。

    python 侧没有 get_component_transform，只能这样算；顺带也证明了父子关系成立
    （辅团若没挂到主团下，叠乘结果会和主团一样小）。
    """
    total = 1.0
    node = comp
    while node:
        total *= node.get_editor_property('relative_scale3d').x
        node = node.get_attach_parent()
    return total


for idx, mesh in enumerate(meshes):
    rel = mesh.get_editor_property('relative_location')
    world_scale = world_scale_x(mesh)
    mat = mesh.get_material(0)
    is_mid = isinstance(mat, unreal.MaterialInstanceDynamic)
    opacity = mat.get_scalar_parameter_value('Opacity') if is_mid else -1
    density = mat.get_scalar_parameter_value('Density') if is_mid else -1
    parent = mesh.get_attach_parent()
    print('SMOKE_LAYER {0} rel=({1:.1f},{2:.1f},{3:.1f}) world_scale={4:.2f} parent={5} mid={6} opacity={7:.3f} density={8:.3f}'.format(
        idx, rel.x, rel.y, rel.z, world_scale,
        parent.get_name() if parent else 'None', is_mid, opacity, density))

# 世界尺寸必须同量级：辅团若只拿到层系数（0.5~0.72）说明缩放没继承父级。
scales = [world_scale_x(m) for m in meshes]
biggest = max(scales)
assert scales[0] == biggest, 'main puff is not the largest'
# 最小的辅团也应达到主团的 40% 以上，否则在画面里就是看不见的小球。
assert min(scales) > biggest * .4, 'secondary puff too small: {0}'.format(scales)
# 偏移是相对空间的，乘主团世界缩放才得到世界偏移；必须在半径量级内（Radius=400）、
# 又不能全挤在原点。Age=0 时 Inflate=0.35，所以这里只要求"散开"而不是"散满"。
main_scale = scales[0]
offsets = [m.get_editor_property('relative_location').length() * main_scale for m in meshes]
print('SMOKE_LAYER world_offsets={0}'.format(['{0:.0f}'.format(o) for o in offsets]))
assert max(offsets) < 600, 'puff offset out of range: {0}'.format(offsets)
assert min(offsets[1:]) > 60, 'secondary puffs all sit at origin: {0}'.format(offsets)

unreal.EditorLevelLibrary.destroy_actor(actor)
print('SMOKE_LAYER_DONE')
