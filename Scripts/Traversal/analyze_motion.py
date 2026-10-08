"""Measured engine-frame intervals and authority action duration from captured state."""
import argparse
import collections
import json
import math
from pathlib import Path
import statistics

parser=argparse.ArgumentParser()
parser.add_argument('directory',type=Path)
args=parser.parse_args()
folder=args.directory.resolve()
report=json.loads((folder/'Report.json').read_text(encoding='utf-8'))
rows=[json.loads(line) for line in (folder/'TraversalMotion.jsonl').read_text().splitlines()]
frames=collections.defaultdict(list)
authority=collections.defaultdict(list)
for row in rows:
    p=next((p for p in row['players'] if p['name']=='EX_A'),None)
    if not p:continue
    if row['peer']=='A' and p['traversal'] in (1,2) and row['frame_delta']>0:
        frames[row['cap']].append(row['frame_delta'])
    if row['peer']=='Server':authority[p['traversal_serial']].append((row['world_time'],p))
runs=[]
for run in report.get('traversal_runs',[]):
    serial=run['serial']+1
    active=authority[serial]
    completed=authority[serial+1]
    if not active or not completed:continue
    # Elapsed is authority movement time; timestamp - elapsed estimates the start.
    starts=[t-p['traversal_elapsed'] for t,p in active if p['traversal']==run['kind']]
    if not starts:continue
    begin=statistics.median(starts)
    end=completed[0][0]
    runs.append(dict(fps_cap=run['fps'],height=run['height'],end=run['end'],
                     observed_completion_seconds=end-begin,last_active_elapsed=max(p['traversal_elapsed'] for _,p in active),
                     configured_duration=.85 if run['kind']==1 else 1.25))
result=dict(result='measured',capture_boundary='Snapshots sample engine frames, not every present. Completion estimates have snapshot and movement-timestamp uncertainty (about 0.15 s). Frame cap is not proof of achieved frame rate.',
            frame_samples={cap:dict(samples=len(v),median_fps=1/statistics.median(v),p95_frame_ms=sorted(v)[min(len(v)-1,math.floor(len(v)*.95))]*1000) for cap,v in frames.items()},
            runs=runs)
(folder/'MotionAnalysis.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result,indent=2))
