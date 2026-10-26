"""Bounded, repeatable extraction of original rifle locomotion (source ZIPs read-only)."""
import hashlib
import json
from pathlib import Path
import sqlite3
import sys

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'Scripts/SpaceCenter'))
from source_packages import extract

OUT = PROJECT / 'Saved/Stinger/Locomotion'
BASE = 'DeltaForce/Content/Models/Weapons/Guns/AR/Anims/Male/'
NAMES = ['3P/AR_M_3P_idle', '3P/AR_M_3P_crouch_Idle', '3P/AR_M_3P_sprint',
         '1P/Locomotion/AR_M_1P_Idle_Pose_2',
         '1P/Locomotion/AR_M_1P_Idle_pose', '1P/Locomotion/AR_M_1P_Walk_Loop',
         '1P/Locomotion/AR_M_1P_Run_Loop', '1P/Locomotion/AR_M_1P_Sprint_Loop']
NAMES += ['3P/AR_M_3P_' + gait + direction for gait in ('Walk', 'Run')
          for direction in ('F','FL','FR','L','R','B','BL','BR')]
NAMES += ['3P/AR_M_3P_crouch_Walk_' + d for d in ('f','fl','fr','l','r','b','bl','br')]
NAMES += ['1P/Locomotion/AR_M_1P_' + n for n in
          ('RunToWalk','WalkToRun','WalkToSprint','SprintToWalk','RunToSprint')]
NAMES += ['3P/AR_M_3P_' + n for n in
          ('IdleAOCenter','IdleAODown','IdleAOUp','CrouchIdleAOCentet','CrouchIdleAODown','CrouchIdleAOUp')]

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect((PROJECT / 'Saved/SpaceCenter/catalog.sqlite').resolve().as_uri() + '?mode=ro', uri=True)
    selected, missing = [], []
    for name in NAMES:
        row = db.execute("select path from entries where package=? and ext='.uasset'", ((BASE+name).lower(),)).fetchone()
        if row: selected.append(row[0][:-7])
        else: missing.append(BASE+name)
    db.close()
    report = extract(selected, recursive=True, limit_gib=1)
    report['selection_missing'] = missing
    (OUT/'Sources.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    for mode in ('inspect','export'):
        request = dict(mode=mode, format='ActorX', source=str(PROJECT/'Saved/SpaceCenter/Source/DeltaForce'),
                       output=str(OUT/mode), packages=selected)
        (OUT/(mode+'-request.json')).write_text(json.dumps(request,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'selected':len(selected),'missing':missing,'dependency_missing':report['missing']},ensure_ascii=True))

if __name__ == '__main__': main()
