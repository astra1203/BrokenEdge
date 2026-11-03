"""Place the project water volumes (2026-11-04 user request #22, project-defined).

浮力室：池沿楼面 Z=491，浅区池底 -110、深区池底约 -720；池子在世界 X -2225..175、Y -7348..-4048
（probe 网格 150 cm，见提交说明）。默认空池，池子西侧池沿放一个注水电闸，拉下后 30 秒注满到 Z=450。
中控桥下：桥面约 Z=1500，沟底 -2300..-3300，两侧地面约 50，沟里零星平台顶在 -266..-370；
水面定在 -600（低于这些平台），开局即满水。

Also creates /Game/Extraction/MapEvents/Water/M_ExtractionWater (translucent surface).
Re-running deletes the previous ExtractionWater actors first.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/place_water.py" -NullRHI -unattended -nosound
"""
import json

import unreal

LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
MATERIAL = '/Game/Extraction/MapEvents/Water/M_ExtractionWater'

WATERS = [
    {'label': 'ME_BuoyancyPoolWater', 'name': '浮力室', 'location': (-1025, -5698, 0),
     'half': (1200, 1650), 'bottom': -720, 'full': 450, 'starts_full': False, 'fill_seconds': 30,
     'switches': [(-1275, -1425, 491)]},
    {'label': 'ME_BridgeRavineWater', 'name': '中控桥下水域', 'location': (4859, -16048, 0),
     'half': (4000, 1850), 'bottom': -3400, 'full': -600, 'starts_full': True, 'fill_seconds': 1,
     'switches': []},
]


def build_material():
    lib = unreal.EditorAssetLibrary
    if lib.does_asset_exist(MATERIAL):
        return unreal.load_asset(MATERIAL)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    folder, name = MATERIAL.rsplit('/', 1)
    material = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('two_sided', True)
    material.set_editor_property('translucency_lighting_mode',
                                 unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mel = unreal.MaterialEditingLibrary
    color = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 0)
    color.set_editor_property('constant', unreal.LinearColor(0.03, 0.14, 0.17, 1))
    opacity = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 200)
    opacity.set_editor_property('r', 0.72)
    rough = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 300)
    rough.set_editor_property('r', 0.06)
    spec = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 400)
    spec.set_editor_property('r', 0.9)
    mel.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(spec, '', unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(material)
    assert lib.save_loaded_asset(material)
    return material


def main():
    material = build_material()
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert levels.load_level(LEVEL)
    for actor in actors_sub.get_all_level_actors():
        if isinstance(actor, unreal.ExtractionWater):
            actors_sub.destroy_actor(actor)
    report = []
    for spec in WATERS:
        water = actors_sub.spawn_actor_from_class(unreal.ExtractionWater, unreal.Vector(*spec['location']),
                                                  unreal.Rotator())
        assert water, spec['label']
        water.set_actor_label(spec['label'])
        water.set_editor_property('half_size', unreal.Vector2D(*spec['half']))
        water.set_editor_property('bottom_z', spec['bottom'])
        water.set_editor_property('full_z', spec['full'])
        water.set_editor_property('starts_full', spec['starts_full'])
        water.set_editor_property('fill_seconds', spec['fill_seconds'])
        water.set_editor_property('label', spec['name'])
        water.set_editor_property('surface_material', material)
        water.set_editor_property('switch_locations', [unreal.Vector(*s) for s in spec['switches']])
        report.append(spec['label'])
    assert levels.save_current_level()
    print('WATER_PLACED', json.dumps(report, ensure_ascii=False))


main()
