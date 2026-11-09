"""Create the native extraction-zone marker material without touching existing maps."""
import unreal

path = '/Game/Extraction/Materials/M_ExtractionExit'
material = unreal.load_asset(path)
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_ExtractionExit', '/Game/Extraction/Materials', unreal.Material, unreal.MaterialFactoryNew())
unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant3Vector)
color.set_editor_property('constant', unreal.LinearColor(.015, .55, .24, 1))
unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
unreal.MaterialEditingLibrary.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material)
unreal.log('EXTRACTION_PHASE4_EXIT_PREPARED')
