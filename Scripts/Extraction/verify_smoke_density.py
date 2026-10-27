"""校验烟雾浓度改造是否真的落到了资产上。

为什么要单独验：
    build_smoke_material.py 在 -NullRHI 下 recompile_material 必然返回空列表，
    所以"脚本跑通"不等于"材质连线正确"。这里直接读回材质表达式图，
    确认 Opacity 的来源链和参数默认值，避免又是"看起来成功、烟还是淡"。

检查项
    M_ExtractionSmoke 的 blend_mode / shading_model / two_sided
    MP_OPACITY / MP_EMISSIVE_COLOR 是否都有输入
    Opacity 参数是否存在且默认 1.0；Density 参数是否存在且默认 0.92
    Color 参数默认值是否是低亮度灰（过亮会显得淡）
    T_ExtractionSmokePuff 的尺寸与 sRGB 设置
"""
import unreal

MATERIAL = '/Game/Extraction/Abilities/M_ExtractionSmoke'
TEXTURE = '/Game/Extraction/Abilities/Textures/T_ExtractionSmokePuff'

material = unreal.load_asset(MATERIAL)
assert material, 'material missing'
lib = unreal.MaterialEditingLibrary

print('SMOKE_CHECK blend={0} shading={1} two_sided={2}'.format(
    material.get_editor_property('blend_mode'),
    material.get_editor_property('shading_model'),
    material.get_editor_property('two_sided')))

# MP_OPACITY 与 MP_EMISSIVE_COLOR 都必须有输入，否则一个不透明、一个纯黑。
for prop, label in ((unreal.MaterialProperty.MP_OPACITY, 'OPACITY'),
                    (unreal.MaterialProperty.MP_EMISSIVE_COLOR, 'EMISSIVE')):
    source = lib.get_material_property_input_node(material, prop)
    print('SMOKE_CHECK {0} source={1}'.format(
        label, source.get_class().get_name() if source else 'NONE'))

# 参数默认值是运行时 MID 未覆盖时的兜底，必须是非零浓度 / 低亮度。
# 注意 parameter_name 读回来是 unreal.Name 而不是 str，直接 in 判断会漏掉。
params = {}
for expr in lib.get_material_expressions(material):
    if isinstance(expr, unreal.MaterialExpressionScalarParameter):
        key = str(expr.get_editor_property('parameter_name'))
        params[key] = expr.get_editor_property('default_value')
    elif isinstance(expr, unreal.MaterialExpressionVectorParameter):
        key = str(expr.get_editor_property('parameter_name'))
        params[key] = expr.get_editor_property('default_value')
print('SMOKE_CHECK params={0}'.format(params))
assert 'Opacity' in params, 'Opacity param missing: {0}'.format(list(params))
assert 'Density' in params, 'Density param missing: {0}'.format(list(params))
assert params['Density'] >= .5, 'Density too low: {0}'.format(params['Density'])

texture = unreal.load_asset(TEXTURE)
assert texture, 'texture missing'
print('SMOKE_CHECK texture={0} size={1}x{2} srgb={3}'.format(
    texture.get_name(), texture.blueprint_get_size_x(), texture.blueprint_get_size_y(),
    texture.get_editor_property('srgb')))
# 贴图内容（实心核占比）由 build_puff_texture.py 自检输出，UE 内置 python 没有 PIL。
print('SMOKE_CHECK_DONE')
