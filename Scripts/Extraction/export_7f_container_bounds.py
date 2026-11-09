"""Read-only UE Editor export of the current authored 7F container candidates.

Set EXTRACTION_RUN_DIR to a fresh directory under this project's Saved, then
run UnrealEditor-Cmd.exe BrokenEdge.uproject -unattended -NullRHI
-run=pythonscript -script=Scripts/Extraction/export_7f_container_bounds.py.
Report.json is written on success/failure; the editor is requested to exit.
No level is loaded, no actor is spawned and no asset/property is saved/edited.
Derived OBBs describe the current native OnRep_Appearance rule, not a runtime
collision sweep or proof that any candidate was selected in a particular raid.
The historical Plan has 54 proposals; the current layout has 45 after placement
floor/capsule validation. Missing Plan ids are reported, never synthesized.
"""
import configparser
import datetime
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import time
import traceback

import unreal


LAYOUT = '/Game/Extraction/Loot/DA_SpaceCenter_Ma1_Loot'
MODE = '/Script/BrokenEdge.ExtractionGameMode'
PLAN = 'Artifacts/Extraction/RestorationEvidence/Phase7FGameplay/Plan.json'
SOURCE_FILES = ('Source/BrokenEdge/Extraction/ExtractionLootActor.cpp',
                'Source/BrokenEdge/Extraction/ExtractionLootActor.h',
                'Source/BrokenEdge/Extraction/ExtractionLootLayout.cpp',
                'Source/BrokenEdge/Extraction/ExtractionGameMode.cpp',
                'Scripts/Extraction/place_7f_loot_guards.py', 'Config/DefaultGame.ini',
                'Scripts/Extraction/export_7f_container_bounds.py', PLAN)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def prop(owner, name):
    try:
        return owner.get_editor_property(name)
    except Exception as exc:
        raise RuntimeError('Required reflected property unavailable: %s.%s' % (owner, name)) from exc


def finite(values, label):
    result = [float(value) for value in values]
    require(all(math.isfinite(value) for value in result), 'Non-finite values: ' + label)
    return result


def vector(value):
    return finite((value.x, value.y, value.z), 'vector')


def quaternion(value):
    result = finite((value.x, value.y, value.z, value.w), 'quaternion')
    require(abs(sum(v * v for v in result) - 1.) < 1e-4, 'Non-unit candidate quaternion')
    return result


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def collect(root, report):
    tracked = {name: sha(root / name) for name in SOURCE_FILES}
    report['input_sha256'] = tracked

    def asset_record(asset):
        path = asset.get_path_name()
        package = path.split('.', 1)[0]
        require(package.startswith('/Game/'), 'Expected a project asset package: ' + path)
        filename = 'Content/' + package[len('/Game/'):] + '.uasset'
        require((root / filename).is_file(), 'Missing asset file for provenance: ' + filename)
        tracked.setdefault(filename, sha(root / filename))
        return path

    report['stage'] = 'resolve configured map definition, layout and loot class defaults'
    config = configparser.ConfigParser(strict=False, interpolation=None)
    config.read(root / 'Config/DefaultGame.ini', encoding='utf-8-sig')
    definition_path = config['/Script/BrokenEdge.ExtractionGameMode']['MapDefinition'].strip()
    definition = unreal.load_asset(definition_path)
    require(definition is not None, 'Missing configured MapDefinition: ' + definition_path)
    report['map_definition'] = dict(config='Config/DefaultGame.ini', configured_path=definition_path,
                                    loaded_asset=asset_record(definition))
    layout = prop(definition, 'loot_layout')
    require(layout is not None, 'Configured MapDefinition has no LootLayout')
    report['layout'] = asset_record(layout)
    require(report['layout'].split('.', 1)[0] == LAYOUT,
            'Configured MapDefinition references a different layout: ' + report['layout'])
    mode_class = unreal.load_class(None, MODE)
    require(mode_class is not None, 'Missing GameMode class: ' + MODE)
    mode_cdo = unreal.get_default_object(mode_class)
    loot_class = prop(mode_cdo, 'loot_actor_class')
    require(loot_class is not None, 'GameMode has no LootActorClass; native fallback is forbidden')
    cdo = unreal.get_default_object(loot_class)
    require(cdo is not None, 'Missing LootActorClass CDO')
    loot_path = asset_record(loot_class)
    require(prop(cdo, 'loose') is False, 'Configured loot CDO is loose, not an authored container')
    offset = prop(cdo, 'visual_offset')
    box = prop(cdo, 'container_bounds')
    require(box is not None, 'Loot CDO has no ContainerBounds component')
    rotation = prop(box, 'relative_rotation')
    rotation_degrees = finite((rotation.pitch, rotation.yaw, rotation.roll), 'ContainerBounds relative rotation')
    scale = vector(prop(box, 'relative_scale3d'))
    report['cdo'] = dict(game_mode_class=mode_class.get_path_name(), loot_actor_class=loot_path,
                         path=cdo.get_path_name(), visual_offset_cm=vector(offset),
                         container_bounds_component=box.get_path_name(),
                         container_relative_rotation_pitch_yaw_roll=rotation_degrees,
                         container_relative_scale3d=scale,
                         collision_enabled_before_onrep=str(box.get_collision_enabled()),
                         pawn_response=str(box.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)),
                         visibility_response=str(box.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY)))
    # Native OnRep changes the box's location/extent, but preserves these two
    # Blueprint defaults. Do not silently derive a different OBB if they change.
    require(all(abs(math.remainder(v, 360.)) < 1e-6 for v in rotation_degrees),
            'Non-identity ContainerBounds relative rotation; exact derived OBB needs an explicit composition update')
    require(all(abs(v - 1.) < 1e-6 for v in scale),
            'Non-unit ContainerBounds relative scale; exact derived OBB needs an explicit composition update')

    report['stage'] = 'read candidate properties and exact skeletal mesh bounds'
    all_points = list(prop(layout, 'points'))
    inventory = [dict(id=str(prop(point, 'id')), kind=str(prop(point, 'kind')),
                      group=str(prop(point, 'group'))) for point in all_points]
    planned = json.loads((root / PLAN).read_text(encoding='utf-8'))['containers']
    expected = {p['id'] for p in planned}
    points = [point for point, row in zip(all_points, inventory) if row['id'] in expected]
    ids = [str(prop(point, 'id')) for point in points]
    counts = Counter(ids)
    # Keep the actual read evidence even when an expected candidate is missing.
    # Prefix membership is diagnostic only: native Kind="Loose" sites do not
    # belong to Plan.containers. Authored containers currently use Kind="Supply",
    # not a guessed enum/string named "Container".
    report['candidate_inventory'] = dict(layout_point_count=len(all_points), layout_points=inventory,
        planned_count=len(planned), planned_unique_count=len(expected), planned_ids=sorted(expected),
        prefix_count=sum(row['id'].startswith('7F_') for row in inventory),
        excluded_prefix_points=[row for row in inventory if row['id'].startswith('7F_') and row['id'] not in expected],
        selected_count=len(ids), selected_ids=sorted(ids), missing_ids=sorted(expected - set(ids)),
        duplicate_selected_ids=sorted(ident for ident, count in counts.items() if count > 1))
    require(len(planned) == len(expected) == 54, 'Plan must contain exactly 54 unique container ids')
    require(bool(ids) and len(ids) == len(set(ids)),
            'No current Plan candidates, or duplicate ids; see candidate_inventory')
    require(all(str(prop(point, 'kind')).casefold() != 'loose' for point in points),
            'A planned container is a native Loose point; see candidate_inventory kinds')
    unplanned_containers = [row['id'] for row in inventory
                            if row['id'].startswith('7F_') and row['id'] not in expected
                            and row['kind'].casefold() != 'loose']
    require(not unplanned_containers,
            'Unplanned current 7F containers need explicit export scope review: ' + str(unplanned_containers))
    report['plan_comparison'] = dict(
        status='current_layout_differs_from_historical_plan' if set(ids) != expected else 'matches_historical_plan',
        historical_plan=PLAN, historical_candidate_count=len(planned), current_candidate_count=len(ids),
        missing_ids=sorted(expected - set(ids)), synthesized_candidates=0,
        boundary='Exports only actual current asset points; missing proposals are not runtime spawn candidates.',
        placement_rule='place_7f_loot_guards.py rechecks each proposal with floor_at and discards failed floor/capsule probes.')
    unreal.StingerAssetLibrary.finish_validation_asset_compilation()
    appearances = report['appearances'] = {}
    records = report['points'] = []
    for point in sorted(points, key=lambda p: str(prop(p, 'id'))):
        ident = str(prop(point, 'id'))
        appearance = prop(point, 'appearance')
        require(appearance is not None, 'Missing appearance on ' + ident)
        appearance_path = asset_record(appearance)
        require(isinstance(appearance, unreal.SkeletalMesh), 'Appearance is not a SkeletalMesh: ' + appearance_path)
        if appearance_path not in appearances:
            bounds = appearance.get_bounds()  # Extended UE bounds, exactly as native OnRep_Appearance uses.
            origin, extent = vector(bounds.origin), vector(bounds.box_extent)
            sphere = finite((bounds.sphere_radius,), 'bounds sphere radius')[0]
            require(all(v >= 0 for v in extent) and sphere > 0, 'Invalid bounds: ' + appearance_path)
            appearances[appearance_path] = dict(bounds_origin_cm=origin, bounds_half_extent_cm=extent,
                                                bounds_sphere_radius_cm=sphere, bounds_api='USkeletalMesh.GetBounds')
        mesh = appearances[appearance_path]
        transform = prop(point, 'transform')
        position, quat, point_scale = vector(transform.translation), quaternion(transform.rotation), vector(transform.scale3d)
        require(all(abs(v) > 1e-6 for v in point_scale), 'Degenerate transform scale: ' + ident)
        weight = finite((prop(point, 'weight'),), ident + ' weight')[0]
        local_center = [mesh['bounds_origin_cm'][i] + vector(offset)[i] + (20. if i == 2 else 0.) for i in range(3)]
        local_extent = [max(v, 5.) for v in mesh['bounds_half_extent_cm']]
        center = vector(transform.transform_location(unreal.Vector(*local_center)))
        corners = []
        for x in (-1, 1):
            for y in (-1, 1):
                for z in (-1, 1):
                    signs = (x, y, z)
                    local = unreal.Vector(*[local_center[i] + signs[i] * local_extent[i] for i in range(3)])
                    corners.append(vector(transform.transform_location(local)))
        recipe = prop(point, 'recipe')
        records.append(dict(id=ident, group=str(prop(point, 'group')), enabled=bool(prop(point, 'enabled')),
                            weight=weight, kind=str(prop(point, 'kind')), label=str(prop(point, 'label')),
                            reference=str(prop(point, 'reference')), appearance=appearance_path,
                            recipe=recipe.get_path_name() if recipe else None,
                            transform=dict(translation_cm=position, rotation_xyzw=quat, scale3d=point_scale),
                            derived_native=dict(scope='Asset/CDO plus native source formula; no runtime actor or sweep',
                                                local_box_center_cm=local_center, local_half_extent_cm=local_extent,
                                                world_box_center_cm=center, interaction_point_world_cm=center,
                                                world_rotation_xyzw=quat,
                                                world_half_extent_cm=[local_extent[i] * abs(point_scale[i]) for i in range(3)],
                                                world_corners_cm=corners)))
    require(len(records) == len(ids), 'Incomplete current-candidate export')
    report['candidate_count'] = len(records)
    report['group_count'] = len({p['group'] for p in records})
    report['input_sha256'] = tracked
    changed = [name for name, before in tracked.items() if sha(root / name) != before]
    require(not changed, 'Inputs changed during read-only export: ' + str(changed))
    report['asset_files_unchanged'] = True


def main():
    started = time.monotonic()
    root = Path(unreal.Paths.project_dir()).resolve()
    report = dict(schema=1, result='running', stage='validate output',
                  started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  scope='Read-only asset and CDO export; no level load, spawn, asset edit/save, PIE or runtime validation')
    output = None
    try:
        value = os.environ.get('EXTRACTION_RUN_DIR')
        require(bool(value), 'Set EXTRACTION_RUN_DIR to an explicit fresh directory under project Saved')
        out = Path(value).resolve()
        saved = (root / 'Saved').resolve()
        require(out != saved and out.is_relative_to(saved), 'EXTRACTION_RUN_DIR must be a child of project Saved')
        out.mkdir(parents=True, exist_ok=True)
        require(not (out / 'Report.json').exists(), 'Refusing to replace an existing Report.json')
        output = out / 'Report.json'
        collect(root, report)
        differs = report['plan_comparison']['status'] != 'matches_historical_plan'
        report.update(result='passed_with_plan_difference' if differs else 'passed', stage='complete', export_complete=True)
        unreal.log('EXTRACTION_7F_CONTAINER_BOUNDS_EXPORTED candidates=%d appearances=%d missing_historical_plan=%d'
                   % (report['candidate_count'], len(report['appearances']), len(report['plan_comparison']['missing_ids'])))
        if differs:
            unreal.log_warning('EXTRACTION_7F_HISTORICAL_PLAN_DIFFERENCE: ' + ', '.join(report['plan_comparison']['missing_ids']))
    except Exception as exc:
        report.update(result='failed', error=str(exc), traceback=traceback.format_exc())
        unreal.log_error('EXTRACTION_7F_CONTAINER_BOUNDS_FAILED: ' + str(exc))
        raise
    finally:
        try:
            report['elapsed_seconds'] = round(time.monotonic() - started, 3)
            if output is not None:
                output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        finally:
            unreal.SystemLibrary.quit_editor()


if __name__ == '__main__':
    main()
