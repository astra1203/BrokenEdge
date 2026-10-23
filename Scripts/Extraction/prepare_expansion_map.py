"""Move the legacy playable fixture into an explicitly temporary data asset.

This preserves existing coordinates; it DOES NOT claim official spawn/exit accuracy.
Run in the editor after compiling ExtractionMapDefinition. No map actors are saved.
"""
import json
from pathlib import Path
import unreal


def run():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    original = editor.get_editor_world().get_path_name().split('.')[0]
    path = '/Game/Extraction/Maps/DA_SpaceCenterGameplay'
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Stop PIE before preparing map data'
    assert levels.load_level('/Game/Maps/SpaceCenter/L_SpaceCenterExtraction')
    try:
        groups = {1: [], 2: []}
        for actor in actors.get_all_level_actors():
            if isinstance(actor, unreal.PlayerStart) and 'ExtractionSpawn' in map(str, actor.tags):
                index = int(str(actor.get_editor_property('player_start_tag'))[-2:])
                groups[1 if index in (0, 2, 3) else 2].append((index, actor.get_actor_transform()))
        assert all(len(values) >= 3 for values in groups.values()), groups
        asset = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
        if asset:
            raise RuntimeError('Map definition already exists; do not overwrite authored point corrections')
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.ExtractionMapDefinition)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'DA_SpaceCenterGameplay', '/Game/Extraction/Maps', unreal.ExtractionMapDefinition, factory)
        records = []
        for squad, values in groups.items():
            group = unreal.ExtractionSpawnGroup()
            group.set_editor_property('id', 'LegacyGroup%d' % squad)
            group.set_editor_property('positions', [v for _, v in sorted(values)])
            group.set_editor_property('reference', 'Existing extraction PlayerStart fixture; official locations unverified')
            records.append(group)
        exit_definition = unreal.ExtractionExitDefinition()
        exit_definition.set_editor_property('id', 'LegacyNorth')
        exit_definition.set_editor_property('label', '北侧撤离点（临时）')
        exit_definition.set_editor_property('transform', unreal.Transform(location=unreal.Vector(1400, 2900, 365)))
        exit_definition.set_editor_property('reference', 'Legacy phase-4 fixture; fixed position and 10 seconds are NOT official rules')
        asset.set_editor_property('map_id', 'SpaceCenter')
        asset.set_editor_property('source_to_world', unreal.Transform(location=unreal.Vector(-664000, 451000, 22400)))
        asset.set_editor_property('spawn_groups', records)
        asset.set_editor_property('exits', [exit_definition])
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
        mode = unreal.get_default_object(unreal.load_class(None, '/Script/BrokenEdge.ExtractionGameMode'))
        mode.set_editor_property('map_definition', asset)
        report = {'status': 'legacy_fixture_migrated_not_official', 'asset': path,
                  'groups': {str(k): len(v) for k, v in groups.items()},
                  'runtime_validation': 'not_run', 'source_coordinate_alignment': 'requires_landmark_check'}
        out = Path(unreal.Paths.project_saved_dir()) / 'Extraction/SpaceCenterExpansion01/MapPreparation.json'
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report, ensure_ascii=False))
    finally:
        assert levels.load_level(original)


if __name__ == '__main__':
    run()
