"""Place the Space Center map events: east drawbridge and the random broken bridge.

Drawbridge: official BP_Interactor_BridgeDoor_2 in SpaceCenter_Ma1/GenericConfig/SpaceCenter_BigInteractor,
root source (681209.0, -447710.3, -21680.35) yaw 45 -> world via the project offset
(-664000, +451000, +22400). Deck meshes SM_I_RoadFacility_Ground_A_11A/11B come from
原始资源ZIP/05_环境场景_007.zip, converted to glTF by the SpaceCenter converter
(Saved/Extraction/MapEvents/deck); the restored neighbouring road SM_I_RoadFacility_Ground_A_10Cc
lends its materials because the converter exports geometry only.

Broken bridge: no source data was found, so the candidates are the three high walkway spans of
the launch-area bridge (X ~ 4881): Bridge_01K at Y -13268, Bridge_01J at Y -15629, Bridge_01K
at Y -17990. Each raid the server hides one of them (AExtractionBrokenBridge).

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/place_map_events.py" -NullRHI -unattended -nosound
"""
import json
import math
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir())
LEVEL = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
DECK_SOURCE = ROOT / ('Saved/Extraction/MapEvents/deck/DeltaForce/Content/Environment/Structures/IRIS/'
                      'RoadFacility/Ground/SpaceCenter_Ma1')
DECK_DEST = '/Game/Extraction/MapEvents/Drawbridge'
ROAD = ('/Game/Maps/SpaceCenter/Restored/Assets/8c97769c2404b22d/SM_I_RoadFacility_Ground_A_10Cc/StaticMeshes/'
        'SM_I_RoadFacility_Ground_A_10Cc')
OFFSET = (-664000.0, 451000.0, 22400.0)
BRIDGE_SOURCE = (681209.0, -447710.3, -21680.35)
SPANS = [('发射区高架北段', 'SCR_Group_4eb3b27df5dfeeeb', (4881, -13268, 1468)),
         ('发射区高架中段', 'SCR_Group_cdaf55fe52ebe94d', (4881, -15629, 1526)),
         ('发射区高架南段', 'SCR_Group_321ebf0e84e00e1b', (4881, -17990, 1468))]
lib = unreal.EditorAssetLibrary


def import_decks():
    road = unreal.load_asset(ROAD)
    materials = [m.material_interface for m in road.static_materials] if road else []
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    restored = {}
    for data in registry.get_assets_by_path('/Game/Maps/SpaceCenter', recursive=True):
        if str(data.asset_class_path.asset_name) in ('MaterialInstanceConstant', 'Material'):
            restored.setdefault(str(data.asset_name), str(data.package_name) + '.' + str(data.asset_name))
    report = []
    decks = []
    for name in ('SM_I_RoadFacility_Ground_A_11A', 'SM_I_RoadFacility_Ground_A_11B'):
        task = unreal.AssetImportTask()
        task.filename = str(DECK_SOURCE / (name + '.glb'))
        task.destination_path = DECK_DEST + '/' + name
        task.automated = True
        task.replace_existing = True
        task.save = True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        meshes = [m for m in (unreal.load_asset(p) for p in lib.list_assets(DECK_DEST + '/' + name, recursive=True))
                  if isinstance(m, unreal.StaticMesh)]
        assert len(meshes) == 1, (name, meshes)
        mesh = meshes[0]
        # 槽位名就是官方材质名：优先用还原地图里同名的材质，没有才借用相邻路面材质。
        for index, slot in enumerate(mesh.static_materials):
            name = str(slot.material_slot_name)
            found = restored.get(name)
            if found:
                mesh.set_material(index, unreal.load_asset(found))
            elif materials:
                mesh.set_material(index, materials[min(index, len(materials) - 1)])
            report.append({'slot': name, 'restored': found})
        body = mesh.get_editor_property('body_setup')
        if body:
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        assert lib.save_loaded_asset(mesh, only_if_is_dirty=False)
        decks.append(mesh)
    print('DECK_MATERIALS', json.dumps(report, ensure_ascii=False))
    return decks


def main():
    decks = import_decks()
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert levels.load_level(LEVEL)
    actors = actors_sub.get_all_level_actors()
    for actor in actors:
        if isinstance(actor, (unreal.ExtractionDrawbridge, unreal.ExtractionBrokenBridge)):
            actors_sub.destroy_actor(actor)
    by_label = {a.get_actor_label(): a for a in actors_sub.get_all_level_actors()}

    location = unreal.Vector(*[s + o for s, o in zip(BRIDGE_SOURCE, OFFSET)])
    bridge = actors_sub.spawn_actor_from_class(unreal.ExtractionDrawbridge, location, unreal.Rotator(yaw=45.0))
    bridge.set_actor_label('ME_EastDrawbridge')
    bridge.get_editor_property('deck1').set_static_mesh(decks[0])
    bridge.get_editor_property('deck2').set_static_mesh(decks[1])

    broken = actors_sub.spawn_actor_from_class(unreal.ExtractionBrokenBridge, unreal.Vector(4881, -15629, 1500),
                                               unreal.Rotator())
    broken.set_actor_label('ME_LaunchBrokenBridge')
    spans, report = [], []
    for label, group, where in SPANS:
        actor = by_label.get(group)
        assert actor, group
        component = actor.get_component_by_class(unreal.InstancedStaticMeshComponent)
        assert component, group
        best = min(range(component.get_instance_count()), key=lambda i: math.dist(
            [component.get_instance_transform(i, True).translation.x,
             component.get_instance_transform(i, True).translation.y,
             component.get_instance_transform(i, True).translation.z], where))
        part = unreal.ExtractionBridgePart()
        part.set_editor_property('actor', actor)
        part.set_editor_property('component', component.get_fname())
        part.set_editor_property('instance', best)
        span = unreal.ExtractionBridgeSpan()
        span.set_editor_property('label', label)
        span.set_editor_property('parts', [part])
        spans.append(span)
        report.append({'label': label, 'actor': group, 'component': str(component.get_fname()), 'instance': best})
    broken.set_editor_property('spans', spans)
    assert levels.save_current_level()
    print('MAP_EVENTS_PLACED', json.dumps({'drawbridge': [location.x, location.y, location.z], 'spans': report},
                                          ensure_ascii=False))


main()
