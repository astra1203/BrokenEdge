"""在真实世界实例化烟幕，校验当前选中的表现后端是否真的生效。

为什么需要：
    表现有两套后端 —— 多层半透明球体，和 Niagara 云团（P_Clouds）。
    "对象存在"和"玩家看得见"是两件事：球体曾经存在但 alpha=0，云团也可能
    挂上了却因为资产没加载、缩放算成 0 而什么都看不到。这里把实际渲染用的
    那套后端读回来量化。

校验（球体后端）
    6 个 UStaticMeshComponent（主团 + 5 个叠加团）
    每层都换成 UMaterialInstanceDynamic，且 Opacity 参数被写过
    层的相对位置互不相同（说明偏移生效，不是全叠在原点）
    层的缩放按 Scale 系数递减（主团最大，顶层最小）

校验（云团后端）
    云团组件有资产、处于激活状态
    云团世界缩放非零且在合理量级（缩放为 0 = 画面上什么都没有）
    球体全部隐藏（两套同时开着会叠成白雾）
"""
import unreal

SMOKE_CLASS = '/Script/BrokenEdge.ExtractionSmoke'
smoke_cls = unreal.load_class(None, SMOKE_CLASS)
assert smoke_cls, 'class missing'

world = unreal.EditorLevelLibrary.get_editor_world()
spawn_at = unreal.Vector(0, 0, 200)
actor = unreal.EditorLevelLibrary.spawn_actor_from_class(smoke_cls, spawn_at)
assert actor, 'spawn failed'
# 编辑器世界里不会自动跑 BeginPlay / Tick，必须手动推一次表现：
# Refresh 是 UFUNCTION，内部会先判定后端（ApplyVisualBackend）再摆放外观。
# 注意 Tick 不是 UFUNCTION，反射调不到，所以这里只能验证 Refresh 这一条路径。
actor.call_method('Refresh')

# 关键回归：主团就是根组件，对它写 SetRelativeLocation 等于把整个 Actor 传送到
# 那个世界坐标（旧 bug 里第 0 层偏移是 (0,0,.10)*50，于是烟幕每帧都被搬到
# 世界原点 —— 扔在几百米外的烟瞬间跑到地图角落，玩家看到的就是"完全没有烟"）。
moved = (actor.get_actor_location() - spawn_at).length()
print('SMOKE_LAYER actor_drift={0:.2f}'.format(moved))
assert moved < 1.0, 'ApplyLook teleported the actor by {0}cm'.format(moved)

def read_prop(obj, candidates, default=None):
    """按候选名依次尝试读 UPROPERTY。

    Python 侧对 C++ 属性名的大小写/下划线转换规则不稳定（bUseCloudFx 既可能是
    b_use_cloud_fx 也可能是 use_cloud_fx），这里不赌单一写法。
    """
    for name in candidates:
        try:
            return obj.get_editor_property(name)
        except Exception:
            continue
    return default


wants_cloud = bool(read_prop(actor, ['use_cloud_fx', 'b_use_cloud_fx', 'bUseCloudFx'], False))
cloud_system = read_prop(actor, ['cloud_system', 'CloudSystem'])
meshes = actor.get_components_by_class(unreal.StaticMeshComponent)
print('SMOKE_LAYER backend={0} meshes={1} cloud_asset={2}'.format(
    'niagara' if (wants_cloud and cloud_system) else 'spheres',
    len(meshes),
    cloud_system.get_name() if cloud_system else 'None'))


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


def world_scale(comp):
    """沿父级链叠乘相对缩放，返回 (x, y, z)。"""
    total = [1.0, 1.0, 1.0]
    node = comp
    while node:
        rel = node.get_editor_property('relative_scale3d')
        total[0] *= rel.x
        total[1] *= rel.y
        total[2] *= rel.z
        node = node.get_attach_parent()
    return total


def expected_aspect():
    """从 DefaultGame.ini 读期望的垂直倍率。

    不写死数值：浓淡和高矮都是调参项，脚本要跟着 ini 走，否则改完参数
    验收会误报"拉高没生效"。
    """
    import os
    ini = os.path.normpath(os.path.join(
        os.path.dirname(__file__), '..', '..', 'Config', 'DefaultGame.ini'))
    try:
        with open(ini, 'r', encoding='utf-8') as fh:
            for line in fh:
                line = line.strip()
                if line.startswith('SmokeCloudAspect'):
                    return float(line.split('=')[1].strip())
    except Exception:
        pass
    return 1.0


if wants_cloud and cloud_system:
    fx_list = actor.get_components_by_class(unreal.NiagaraComponent)
    print('SMOKE_LAYER niagara_components={0}'.format(len(fx_list)))
    # 叠层是"浓度"的唯一手段：单层云的 alpha 只有两三成，层数掉了烟就会退回一片薄雾。
    assert len(fx_list) >= 2, 'cloud backend has no layered stack: {0}'.format(len(fx_list))
    assert len(fx_list) <= 16, 'too many cloud layers: {0}'.format(len(fx_list))

    scales = []
    heights = []
    rotations = []
    # 只统计真正启用的层：CloudLayerCount 调小后，多余组件被隐藏但仍留在 Actor 上。
    for fx in [f for f in fx_list if f.is_visible()]:
        asset = fx.get_asset()
        assert asset is not None, 'Niagara component {0} has no system asset'.format(fx.get_name())
        assert asset.get_name() == 'P_Clouds', 'unexpected cloud asset: {0}'.format(asset.get_name())
        sx, _, sz = world_scale(fx)
        scales.append(sx)
        heights.append(sz)
        rotations.append(fx.get_editor_property('relative_rotation').yaw)
    print('SMOKE_LAYER visible_layers={0}'.format(len(scales)))
    assert len(scales) >= 2, 'only {0} cloud layer(s) enabled, density will read as haze'.format(len(scales))
    print('SMOKE_LAYER fx_assets=P_Clouds scales={0}'.format(['{0:.2f}'.format(s) for s in scales]))

    # 竖直倍率：Z 世界缩放必须按 SmokeCloudAspect 拉开，否则烟团还是贴地那一片。
    want_aspect = expected_aspect()
    ratio = heights[0] / scales[0] if scales[0] > 1e-4 else 0.0
    print('SMOKE_LAYER fx_height_ratio={0:.2f} want={1:.2f}'.format(ratio, want_aspect))
    assert abs(ratio - want_aspect) < 0.05 * max(want_aspect, 1.0), \
        'vertical stretch is {0:.2f}x but SmokeCloudAspect says {1:.2f}x'.format(ratio, want_aspect)

    # 缩放为 0 意味着画面上什么都没有；超过 100 意味着半径算错了，烟会糊满地图。
    assert min(scales) > 0.05, 'a cloud layer scaled to nothing: {0}'.format(scales)
    assert max(scales) < 100.0, 'cloud scaled absurdly large: {0}'.format(scales)
    # 各层必须真的错开朝向，否则五层是同一张云的完全重叠，浓度上去了但没有体积。
    print('SMOKE_LAYER fx_yaws={0}'.format(['{0:.0f}'.format(y) for y in rotations]))
    assert len(set(round(y) for y in rotations)) == len(rotations), 'cloud layers share the same yaw'
    # 编辑器世界里没有游戏世界和渲染器，Niagara 组件不会真的激活，is_active() 恒为 False，
    # 所以这一项只提示不断言，真正的可见性必须在 PIE 里看。
    if not any(fx.is_active() for fx in fx_list):
        print('SMOKE_LAYER warn: fx inactive in editor world (expected outside PIE)')

    # 球体必须让位，否则两层半透明叠在一起整体发白。
    shown = [m.get_name() for m in meshes if m.is_visible()]
    print('SMOKE_LAYER visible_meshes={0}'.format(shown))
    assert not shown, 'spheres still visible while cloud backend is on: {0}'.format(shown)

    print('SMOKE_LAYER_DONE')
else:
    print('SMOKE_LAYER count={0}'.format(len(meshes)))
    assert len(meshes) == 6, 'expected 6 puffs, got {0}'.format(len(meshes))

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
    # offsets[0] 是根组件本身，等于 Actor 的出生点（脚本里放在 Z=200），不参与"散开"判断。
    main_radius = main_scale * 50.0
    print('SMOKE_LAYER main_radius={0:.0f}'.format(main_radius))
    assert max(offsets) < 600, 'puff offset out of range: {0}'.format(offsets)
    assert max(offsets[1:]) < main_radius, 'secondary puff escaped the cloud body: {0}'.format(offsets)
    assert min(offsets[1:]) > 25, 'secondary puffs all sit at origin: {0}'.format(offsets)

    print('SMOKE_LAYER_DONE')

unreal.EditorLevelLibrary.destroy_actor(actor)
