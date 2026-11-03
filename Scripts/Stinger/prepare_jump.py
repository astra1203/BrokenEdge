"""Bounded extraction of the original third-person rifle jump set (source ZIPs read-only).

Same pipeline as prepare_locomotion.py, separate work directory so the locomotion
evidence stays untouched:

    <engine python> Scripts/Stinger/prepare_jump.py
    dotnet Scripts/SpaceCenter/Converter/bin/Release/net10.0/SpaceCenter.Converter.dll Saved/Stinger/Jump/inspect-request.json
    dotnet ... Saved/Stinger/Jump/export-request.json
    UnrealEditor-Cmd ... -script=Scripts/Stinger/import_jump.py
"""
import json
from pathlib import Path
import sqlite3
import sys

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'Scripts/SpaceCenter'))
from source_packages import extract

OUT = PROJECT / 'Saved/Stinger/Jump'
BASE = 'DeltaForce/Content/Models/Weapons/Guns/AR/Anims/Male/3P/AR_M_3P_'
NAMES = ['StandJumpstart', 'StandJumpLoop', 'StandJumpLand', 'RunJumpstart', 'RunJumploop', 'RunJumpLand',
         'FallLoopH', 'JumpLandRun']


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect((PROJECT / 'Saved/SpaceCenter/catalog.sqlite').resolve().as_uri() + '?mode=ro', uri=True)
    selected, missing = [], []
    for name in NAMES:
        row = db.execute("select path from entries where package=? and ext='.uasset'",
                         ((BASE + name).lower(),)).fetchone()
        if row:
            selected.append(row[0][:-7])
        else:
            missing.append(BASE + name)
    db.close()
    report = extract(selected, recursive=True, limit_gib=1)
    report['selection_missing'] = missing
    (OUT / 'Sources.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    for mode in ('inspect', 'export'):
        request = dict(mode=mode, format='ActorX', source=str(PROJECT / 'Saved/SpaceCenter/Source/DeltaForce'),
                       output=str(OUT / mode), packages=selected)
        (OUT / (mode + '-request.json')).write_text(json.dumps(request, ensure_ascii=False, indent=2),
                                                    encoding='utf-8')
    print(json.dumps({'selected': len(selected), 'missing': missing, 'dependency_missing': report['missing']},
                     ensure_ascii=True))


if __name__ == '__main__':
    main()
