"""Switch the Square exit from the timed stand-in to the official lever mechanism.

Official GameplayConfig: BP_PlayerExit_Square (PlayerExitId 308) opens by Switch --
four BP_ExitTriggerSquare* levers, two chosen at random each raid, all chosen ones
pulled -> the exit opens, LimitedTime 120 s later it closes. The lever positions come
from `OfficialSpawnExitAnchors.json` (`exit_triggers`). This patch only rewrites the
Square entry of the existing map definition; every other exit and spawn group stays.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/patch_square_switches.py" -NullRHI -unattended -nosound
"""
import json
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir())
ASSET = '/Game/Extraction/Maps/DA_SpaceCenterGameplay'
ANCHORS = ROOT / 'Artifacts/Extraction/RestorationEvidence/SpawnExit/OfficialSpawnExitAnchors.json'
EXIT_ID = 'OfficialExitSquare'
REQUIRED = 2


def switch_settings(anchors):
    """(locations, required, open window) for the Square exit from the anchor snapshot."""
    row = next(e for e in anchors['exits'] if e['id'] == EXIT_ID)
    assert row['open_type'] == 'Switch', row
    triggers = [t for t in anchors['exit_triggers'] if t['actor'].startswith('BP_ExitTriggerSquare')]
    assert len(triggers) == 4, triggers
    return ([unreal.Vector(*t['world']) for t in triggers], REQUIRED,
            float(row['close_seconds_after_open']))


def main():
    anchors = json.loads(ANCHORS.read_text(encoding='utf-8'))
    locations, required, window = switch_settings(anchors)
    asset = unreal.load_asset(ASSET)
    assert asset, ASSET
    exits = list(asset.get_editor_property('exits'))
    index = next(i for i, e in enumerate(exits) if str(e.get_editor_property('id')) == EXIT_ID)
    exit_def = exits[index]
    try:
        exit_def.get_editor_property('switches_required')
    except Exception:
        raise SystemExit('Switch fields are missing: build the editor module first.')
    before = {k: exit_def.get_editor_property(k) for k in ('opens_after_seconds', 'closes_after_seconds')}
    exit_def.set_editor_property('switch_locations', locations)
    exit_def.set_editor_property('switches_required', required)
    exit_def.set_editor_property('switch_open_seconds', window)
    # 电闸决定开放时刻，不再用 ExpectedOpenTime 900 s 定时代替。
    exit_def.set_editor_property('opens_after_seconds', 0.0)
    exit_def.set_editor_property('closes_after_seconds', 0.0)
    exit_def.set_editor_property('reference', '官方 BP_PlayerExit_Square / PlayerExitId=308。'
                                 '电闸式：4 个 BP_ExitTriggerSquare 每局随机启用 2 个，全部拉下后开放 120 秒。')
    exits[index] = exit_def
    asset.set_editor_property('exits', exits)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    where = exit_def.get_editor_property('transform').translation
    report = {'exit': EXIT_ID, 'exit_world': [where.x, where.y, where.z], 'before': {k: float(v) for k, v in before.items()},
              'switch_locations': [[v.x, v.y, v.z] for v in locations],
              'switches_required': required, 'switch_open_seconds': window}
    print('SQUARE_SWITCHES_PATCHED', json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
