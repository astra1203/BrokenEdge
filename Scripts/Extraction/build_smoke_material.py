"""导入烟团贴图并重建 M_ExtractionSmoke。

为什么需要本脚本：
    项目里的 M_ExtractionSmoke 是最早的原型材质（BLEND_Opaque + Noise + Multiply），
    表现成一颗灰色实心球。真正的烟幕需要软边、半透明、可随时间淡入淡出。

贴图来源
    Saved/SmokeSources/T_PuffBlob.png
    由 build_puff_texture.py 程序生成的团块：中心实心、边缘蓬松。
    注意不要用解包的 T_EFX_SmokeSubUV_41_6X6_02.png —— 它的 alpha 均值仅 10/255，
    几乎全透明，当遮罩用会让烟淡到看不见。

产物
    /Game/Extraction/Abilities/Textures/T_ExtractionSmokePuff   烟团团块（遮罩）
    /Game/Extraction/Abilities/M_ExtractionSmoke                烟材质（Opacity/Density/Color）
    /Game/Extraction/Abilities/M_ExtractionSmokeShell           投掷弹体材质

引擎限制（实测 UE 5.8 commandlet）
    MaterialEditingLibrary.delete_all_material_expressions 会触发
    !IsRooted() 断言并终止进程，因此本脚本从不删旧材质，而是整份重建后覆盖同名资产。

幂等：贴图每次覆盖导入（换图必须生效）；材质每次整份重建。
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
    """覆盖导入团块贴图。

    不改压缩/sRGB/LODGroup：这些 setter 在 commandlet 下同样会触发断言，
    默认导入参数（RGBA8 + 完整 mip 链）对烟雾贴图已够用。
    """
    task = unreal.AssetImportTask()
    task.filename = str(SOURCE_DIR / 'T_PuffBlob.png')
    task.destination_path = TEXTURE_DIR
    task.destination_name = PUFF_NAME
    task.automated = True
    task.save = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset('{0}/{1}'.format(TEXTURE_DIR, PUFF_NAME))
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
    """烟材质：贴图当浓度遮罩（RGB），Opacity 控制整体浓淡，颜色由参数控制。

    早期版本把贴图 RGB 当自发光颜色，结果烟的颜色跟着贴图走、整体发亮发淡。
    这里改成：贴图只提供"哪里浓"，颜色用 Color 参数（低亮度灰）单独给。
    """
    material = fresh_material('M_ExtractionSmoke')
    lib = unreal.MaterialEditingLibrary
    sample = lib.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -780, 0)
    sample.set_editor_property('parameter_name', 'Puff')
    sample.set_editor_property('texture', texture)

    opacity = lib.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -780, 300)
    opacity.set_editor_property('parameter_name', 'Opacity')
    opacity.set_editor_property('default_value', 1.0)
    # Density 是每团烟的基础浓度，把遮罩从"半透明薄片"抬到"挡视线"的程度。
    density = lib.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -780, 440)
    density.set_editor_property('parameter_name', 'Density')
    density.set_editor_property('default_value', 1.0)

    # 浓度 = (贴图遮罩 × 0.65 + 0.35) × Opacity × Density
    #
    # 那个 +0.35 是保底：贴图只是在"哪里有烟"上做细节，不该决定"有没有烟"。
    # 上一版直接用贴图当遮罩，贴图一旦导入异常（全黑 / alpha 丢失）整片 Opacity
    # 就是 0，画面上一团空气 —— 而且参数调到 1 也救不回来。保底 0.35 让这类
    # 素材问题降级成"边缘不那么蓬松"，而不是"什么都看不见"。
    detail = lib.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -640, 60)
    lib.connect_material_expressions(sample, 'RGB', detail, 'A')
    detail_scale = lib.create_material_expression(
        material, unreal.MaterialExpressionConstant, -640, 200)
    detail_scale.set_editor_property('r', 0.65)
    lib.connect_material_expressions(detail_scale, '', detail, 'B')
    floor_add = lib.create_material_expression(
        material, unreal.MaterialExpressionAdd, -500, 100)
    lib.connect_material_expressions(detail, '', floor_add, 'A')
    floor_value = lib.create_material_expression(
        material, unreal.MaterialExpressionConstant, -500, 240)
    floor_value.set_editor_property('r', 0.35)
    lib.connect_material_expressions(floor_value, '', floor_add, 'B')

    mask = lib.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -340, 120)
    lib.connect_material_expressions(floor_add, '', mask, 'A')
    lib.connect_material_expressions(opacity, '', mask, 'B')
    boost = lib.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -180, 160)
    lib.connect_material_expressions(mask, '', boost, 'A')
    lib.connect_material_expressions(density, '', boost, 'B')
    lib.connect_material_property(boost, '', unreal.MaterialProperty.MP_OPACITY)

    # 颜色独立于贴图：低亮度中性灰，避免烟比环境还亮（发亮=显得淡）。
    color = lib.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -520, -200)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(.30, .31, .31, 1))
    lib.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    # 默认的 mask clip 阈值是 0.333，边缘半透明像素会被整片裁掉，烟看起来是"碎块"。
    # 我们的浓度最低也有 0.35，理论上刚好躲过，但没必要把余量押在默认值上。
    # 用 try 包住：这个 setter 在 commandlet 下不是必需项，失败也不该拖垮整个材质。
    try:
        material.set_editor_property('opacity_mask_clip_value', 0.0)
    except Exception as error:  # noqa: BLE001
        unreal.log_warning('SMOKE_MATERIAL opacity_mask_clip_value skipped: {0}'.format(error))
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
