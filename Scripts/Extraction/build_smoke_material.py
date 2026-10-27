"""导入烟团贴图并重建 M_ExtractionSmoke。

为什么需要本脚本：
    项目里的 M_ExtractionSmoke 是最早的原型材质（BLEND_Opaque + Noise + Multiply），
    表现成一颗灰色实心球。真正的烟幕需要软边、半透明、可随时间淡入淡出。

贴图来源
    Saved/SmokeSources/T_EFX_SmokeSubUV_41_6X6_02.png
    三角洲解包 Effects 包中的 6x6 序列烟雾图集（白色有效），用作基础形状。

产物
    /Game/Extraction/Abilities/Textures/T_ExtractionSmokePuff   灰度烟团（通道即遮罩）
    /Game/Extraction/Abilities/M_ExtractionSmoke                半透明烟材质（参数 Opacity）
    /Game/Extraction/Abilities/M_ExtractionSmokeShell           投掷弹体材质

引擎限制（实测 UE 5.8 commandlet）
    MaterialEditingLibrary.delete_all_material_expressions 会触发
    !IsRooted() 断言并终止进程，因此本脚本从不删旧材质，而是整份重建后覆盖同名资产。

幂等：贴图已存在时不再导入；材质每次整份重建。
"""
from pathlib import Path

import unreal

PROJECT = Path(unreal.Paths.project_dir())
SOURCE_DIR = PROJECT / 'Saved/SmokeSources'
TEXTURE_DIR = '/Game/Extraction/Abilities/Textures'
PUFF_NAME = 'T_ExtractionSmokePuff'
MATERIAL_DIR = '/Game/Extraction/Abilities'
MATERIAL_NAMES = ['M_ExtractionSmoke', 'M_ExtractionSmokeShell']


def import_texture():
    """把灰度烟团图集导入；已导入时直接复用。

    不改压缩/sRGB/LODGroup：这些 setter 在 commandlet 下同样会触发断言，
    默认导入参数（RGBA8 + 完整 mip 链）对烟雾贴图已够用。
    """
    asset_path = '{0}/{1}'.format(TEXTURE_DIR, PUFF_NAME)
    if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        task = unreal.AssetImportTask()
        task.filename = str(SOURCE_DIR / 'T_EFX_SmokeSubUV_41_6X6_02.png')
        task.destination_path = TEXTURE_DIR
        task.destination_name = PUFF_NAME
        task.automated = True
        task.save = True
        task.replace_existing = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(asset_path)
    assert texture, 'texture import failed'
    return texture


def fresh_material(name):
    """删除同名资产后新建，绕开 commandlet 下清空表达式节点的断言。"""
    path = '{0}/{1}'.format(MATERIAL_DIR, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        assert unreal.EditorAssetLibrary.delete_asset(path), 'cannot delete ' + path
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    assert material, 'cannot create ' + path
    return material


def build_smoke_material(texture):
    """烟材质：贴图 alpha 控制软边，Opacity 参数控制整团浓淡。"""
    material = fresh_material('M_ExtractionSmoke')
    lib = unreal.MaterialEditingLibrary
    sample = lib.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -720, 0)
    sample.set_editor_property('parameter_name', 'Puff')
    sample.set_editor_property('texture', texture)

    opacity = lib.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -720, 300)
    opacity.set_editor_property('parameter_name', 'Opacity')
    opacity.set_editor_property('default_value', 1.0)

    # 每像素透明度 = 贴图 alpha（软边） × 整体不透明度（淡入淡出）。
    alpha = lib.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -400, 160)
    lib.connect_material_expressions(sample, 'A', alpha, 'A')
    lib.connect_material_expressions(opacity, '', alpha, 'B')
    # 材质输出是属性不是表达式节点，必须用 connect_material_property。
    lib.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)

    # 颜色取贴图 RGB 再乘一个压暗系数，避免烟比环境还亮。
    tint = lib.create_material_expression(
        material, unreal.MaterialExpressionConstant3Vector, -560, -240)
    tint.set_editor_property('constant', unreal.LinearColor(.62, .62, .62, 1))
    shade = lib.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -400, -140)
    lib.connect_material_expressions(sample, 'RGB', shade, 'A')
    lib.connect_material_expressions(tint, '', shade, 'B')
    lib.connect_material_property(shade, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    # -NullRHI 下没有着色器编译设备，recompile_material 必然返回 false，
    # 因此结果只能作为日志参考，不能当保存失败判据；真正落盘看 save_loaded_asset。
    compiled = lib.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material), 'material save failed'
    unreal.log('SMOKE_MATERIAL M_ExtractionSmoke compiled={0}'.format(compiled))
    return material


def build_shell_material():
    """弹体材质：不透明深灰纯色。"""
    material = fresh_material('M_ExtractionSmokeShell')
    lib = unreal.MaterialEditingLibrary
    color = lib.create_material_expression(
        material, unreal.MaterialExpressionConstant3Vector, -320, 0)
    color.set_editor_property('constant', unreal.LinearColor(.18, .19, .18, 1))
    lib.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    compiled = lib.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material), 'shell material save failed'
    unreal.log('SMOKE_MATERIAL M_ExtractionSmokeShell compiled={0}'.format(compiled))
    return material


def main():
    texture = import_texture()
    smoke = build_smoke_material(texture)
    shell = build_shell_material()
    print('SMOKE_ASSETS_OK texture={0} material={1} shell={2}'.format(
        texture.get_name(), smoke.get_name(), shell.get_name()))


main()
