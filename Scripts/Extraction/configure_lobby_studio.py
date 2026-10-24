"""Project-authored neutral studio material; no claim of source lobby geometry."""
import unreal
lib=unreal.EditorAssetLibrary
path='/Game/Extraction/UI/M_LobbyStudio'
material=unreal.load_asset(path) if lib.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_LobbyStudio','/Game/Extraction/UI',unreal.Material,unreal.MaterialFactoryNew())
unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
for color,prop in [(unreal.LinearColor(.055,.075,.08,1),unreal.MaterialProperty.MP_BASE_COLOR),
                   (unreal.LinearColor(.008,.012,.014,1),unreal.MaterialProperty.MP_EMISSIVE_COLOR)]:
    expression=unreal.MaterialEditingLibrary.create_material_expression(material,unreal.MaterialExpressionConstant3Vector)
    expression.set_editor_property('constant',color)
    unreal.MaterialEditingLibrary.connect_material_property(expression,'',prop)
roughness=unreal.MaterialEditingLibrary.create_material_expression(material,unreal.MaterialExpressionConstant)
roughness.set_editor_property('r',.8)
unreal.MaterialEditingLibrary.connect_material_property(roughness,'',unreal.MaterialProperty.MP_ROUGHNESS)
unreal.MaterialEditingLibrary.recompile_material(material)
assert lib.save_loaded_asset(material)
print('LOBBY_STUDIO_CONFIGURED')
