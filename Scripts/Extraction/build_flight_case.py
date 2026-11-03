"""Build the project flight case (航空箱) from primitives and use it for crates (2026-11-04 user request #16).

Original geometry, no imported assets: a 110x56x42 cm olive body with aluminium rims, edge rails,
corner caps, two front latches and side handles, plus a separate 112x58x12 cm lid whose pivot is
the hinge line on the back top edge. AExtractionLootActor puts the lid at LidHinge and swings it
open about X when the case is first searched.

Points updated in DA_SpaceCenter_Ma1_Loot:
  FieldCrate (户外物资箱) -> 航空箱 look and label
  Supply     (室内补给箱) -> 航空箱 look, labels kept (they used the large safe model before)

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/build_flight_case.py" -NullRHI -unattended -nosound
"""
import json

import unreal

FOLDER = '/Game/Extraction/Loot/FlightCase'
LAYOUT = '/Game/Extraction/Loot/DA_SpaceCenter_Ma1_Loot'
LIB = unreal.EditorAssetLibrary
GS = unreal.GeometryScript_Primitives
BODY, METAL = 0, 1
L, D, H = 110.0, 56.0, 42.0
LID_T = 12.0
HINGE = (0.0, D / 2 + 1, H)


def material(name, color, metallic, roughness):
    path = '%s/%s' % (FOLDER, name)
    if LIB.does_asset_exist(path):
        LIB.delete_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    mel = unreal.MaterialEditingLibrary
    c = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    c.set_editor_property('constant', unreal.LinearColor(*color, 1))
    m = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 150)
    m.set_editor_property('r', metallic)
    r = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 250)
    r.set_editor_property('r', roughness)
    mel.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(m, '', unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(r, '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    assert LIB.save_loaded_asset(mat)
    return mat


def box(mesh, mat_id, center_xy, z0, size):
    """Axis-aligned box: centre in XY, bottom at z0, size (x, y, z)."""
    options = unreal.GeometryScriptPrimitiveOptions()
    options.set_editor_property('material_id', mat_id)
    xf = unreal.Transform(location=unreal.Vector(center_xy[0], center_xy[1], z0))
    GS.append_box(mesh, options, xf, size[0], size[1], size[2], 0, 0, 0,
                  unreal.GeometryScriptPrimitiveOriginMode.BASE)


def body_mesh():
    mesh = unreal.DynamicMesh()
    box(mesh, BODY, (0, 0), 0, (L, D, H))
    # Aluminium rims: bottom band and top lip, slightly proud of the shell.
    box(mesh, METAL, (0, 0), 0, (L + 2, D + 2, 3))
    box(mesh, METAL, (0, 0), H - 3, (L + 2, D + 2, 3))
    # Vertical edge rails and corner caps.
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * L / 2, sy * D / 2
            box(mesh, METAL, (x, y), 0, (3.2, 3.2, H))
            box(mesh, METAL, (x, y), 0, (6, 6, 6))
            box(mesh, METAL, (x, y), H - 6, (6, 6, 6))
    # Front latches (front is -Y) and end handles.
    for sx in (-1, 1):
        box(mesh, METAL, (sx * 30, -D / 2 - 1.2), H - 11, (8, 2.4, 10))
        box(mesh, METAL, (sx * (L / 2 + 1.5), 0), H - 14, (3, 16, 3))
        box(mesh, METAL, (sx * (L / 2 + 1), -7), H - 18, (2, 2, 6))
        box(mesh, METAL, (sx * (L / 2 + 1), 7), H - 18, (2, 2, 6))
    return mesh


def lid_mesh():
    """Pivot on the hinge line; the lid extends towards -Y."""
    mesh = unreal.DynamicMesh()
    w, d = L + 2, D + 2
    box(mesh, BODY, (0, -d / 2), 0, (w, d, LID_T))
    box(mesh, METAL, (0, -d / 2), 0, (w + 1, d + 1, 3))
    for sx in (-1, 1):
        for y in (-1.0, -d + 1.0):
            box(mesh, METAL, (sx * w / 2, y), 0, (6, 6, LID_T))
        box(mesh, METAL, (sx * 30, -d - 0.6), 0, (8, 2.4, 4))
    # Two raised ribs on top so the lid reads as a moulded case.
    for sy in (-0.3, -0.7):
        box(mesh, BODY, (0, d * sy), LID_T, (w - 16, 4, 1.5))
    return mesh


def save_mesh(mesh, name, materials):
    path = '%s/%s' % (FOLDER, name)
    if LIB.does_asset_exist(path):
        LIB.delete_asset(path)
    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    options.set_editor_property('enable_collision', False)
    result = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, path, options)
    asset = result[0] if isinstance(result, tuple) else result
    assert asset, name
    for index, mat in enumerate(materials):
        asset.set_material(index, mat)
    assert LIB.save_loaded_asset(asset)
    return asset


def main():
    body_mat = material('M_FlightCase_Body', (0.10, 0.12, 0.08), 0.0, 0.62)
    metal_mat = material('M_FlightCase_Metal', (0.62, 0.62, 0.60), 1.0, 0.32)
    body = save_mesh(body_mesh(), 'SM_FlightCase_Body', [body_mat, metal_mat])
    lid = save_mesh(lid_mesh(), 'SM_FlightCase_Lid', [body_mat, metal_mat])

    layout = unreal.load_asset(LAYOUT)
    points = list(layout.get_editor_property('points'))
    changed = {'FieldCrate': 0, 'Supply': 0}
    for point in points:
        kind = str(point.get_editor_property('kind'))
        if kind not in changed:
            continue
        point.set_editor_property('static_appearance', body)
        point.set_editor_property('lid_appearance', lid)
        point.set_editor_property('lid_hinge', unreal.Vector(*HINGE))
        if kind == 'FieldCrate':
            point.set_editor_property('label', '航空箱')
        changed[kind] += 1
    layout.set_editor_property('points', points)
    assert LIB.save_loaded_asset(layout)
    print('FLIGHT_CASE_BUILT', json.dumps({'body': body.get_path_name(), 'lid': lid.get_path_name(),
                                           'points': changed}, ensure_ascii=False))


main()
