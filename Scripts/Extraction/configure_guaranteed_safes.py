"""Make every authored safe a guaranteed spawn.

The Ma1 loot layout used to place the President and H3 safes into one-of-N
compatibility groups (ProjectPresidentOneOfThree / ProjectH3OneOfTwo), so only
one candidate per group existed in a raid. Gameplay now wants every safe to be
present: each point is promoted to a fixed point by clearing its group name, and
``UExtractionLootLayout::Select`` then selects it unconditionally.

Disabled points are left disabled: they failed the floor audit (one is authored
~5.4 km below the map, one has no collision under it) and are not spawnable.

Run with the editor closed:

    UnrealEditor-Cmd.exe BrokenEdge.uproject -unattended -NullRHI -run=pythonscript \
        -script="Scripts/Extraction/configure_guaranteed_safes.py"

Idempotent: a point that already has no group is left untouched.

NOTE: values read out of a TArray property are struct copies and mutating them in
place is silently discarded, so each point is rebuilt as a fresh
``unreal.ExtractionLootPoint`` and the whole array is written back.
"""
import json
from pathlib import Path
import unreal

LAYOUTS = [
    '/Game/Extraction/Loot/DA_SpaceCenter_Ma1_Loot',
    '/Game/Extraction/Loot/DA_SpaceCenter_Level2_Loot',
    '/Game/Extraction/Loot/DA_SpaceCenter_Level31_Loot',
    '/Game/Extraction/Loot/DA_SpaceCenter_Level51_Loot',
]

GROUP_POLICY = ('Ma1 source coordinates; every enabled safe is a fixed point and spawns in '
                'every raid (project tuning). Drops are project recipes.')

FIELDS = ('id', 'weight', 'enabled', 'transform', 'label', 'kind', 'reference', 'appearance', 'recipe')

lib = unreal.EditorAssetLibrary
report = []
touched = 0
for path in LAYOUTS:
    if not lib.does_asset_exist(path):
        unreal.log_warning('GUARANTEED_SAFES_MISSING_LAYOUT ' + path)
        report.append({'layout': path, 'missing': True})
        continue
    layout = lib.load_asset(path)
    rebuilt = []
    cleared = []
    for source in layout.get_editor_property('points'):
        group = str(source.get_editor_property('group'))
        point = unreal.ExtractionLootPoint()
        for field in FIELDS:
            point.set_editor_property(field, source.get_editor_property(field))
        point.set_editor_property('group', '')
        if group not in ('None', ''):
            cleared.append((str(source.get_editor_property('id')), group))
        rebuilt.append(point)
    if cleared:
        layout.set_editor_property('points', rebuilt)
        layout.set_editor_property('group_policy', GROUP_POLICY)
        assert lib.save_loaded_asset(layout, only_if_is_dirty=False), path
        touched += len(cleared)

    # Re-read the saved asset and prove every enabled point is unconditional:
    # either ungrouped, or the only member of its group.
    saved = lib.load_asset(path)
    enabled = 0
    grouped = 0
    for point in saved.get_editor_property('points'):
        group = str(point.get_editor_property('group'))
        if group not in ('None', ''):
            grouped += 1
        if point.get_editor_property('enabled'):
            enabled += 1
    assert grouped == 0, (path, 'groups remain', grouped)
    unreal.log_warning('GUARANTEED_SAFES_PASS %s enabled=%d groups=%d cleared=%d'
                       % (path, enabled, grouped, len(cleared)))
    report.append({'layout': path, 'enabled': enabled, 'groups': grouped,
                   'cleared': [{'id': i, 'group': g} for i, g in cleared]})

out = Path(unreal.Paths.project_dir()) / 'Docs/Extraction/RestorationEvidence/Loot/GuaranteedSafes.json'
out.parent.mkdir(parents=True, exist_ok=True)
# Keep a cumulative record: a rerun is idempotent and clears nothing, but it must not
# erase the evidence of which points were de-grouped the first time.
history = []
if out.exists():
    try:
        history = json.loads(out.read_text(encoding='utf-8')).get('cleared_history', [])
    except ValueError:
        history = []
for entry in report:
    for item in entry.get('cleared', []):
        row = {'layout': entry['layout'], 'id': item['id'], 'group': item['group']}
        if row not in history:
            history.append(row)
out.write_text(json.dumps({'cleared_this_run': touched, 'cleared_history': history, 'layouts': report},
                          ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
unreal.log_warning('GUARANTEED_SAFES_COMPLETE cleared=%d' % touched)
