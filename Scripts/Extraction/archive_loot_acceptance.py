"""Archive compact, reproducible loot checks; never copy player accounts or full snapshots."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('runs',nargs='+');args=parser.parse_args()
out=root/'Artifacts/Extraction/RestorationEvidence/Loot';runs=[];points={}
for tag in args.runs:
    run=root/'Saved/Extraction'/tag;path=run/'Report.json';report=json.loads(path.read_text(encoding='utf-8'))
    assert report['result']=='passed',tag
    names=[c['name'] for c in report['checks']]
    runs.append({'run':tag,'result':report['result'],'checks':names,'report_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                 'seed':report['options'].get('loot_seed')})
    selected=[n.rsplit(' ',1)[-1] for n in names if n.startswith('Authored safe searchable ')]
    for i,point in enumerate(selected):
        images=list(run.glob('A-safe-world-%02d-*.png'%i))
        if images:
            image=max(images,key=lambda p:int(p.stem.rsplit('-',1)[-1]))
            name=point+'.png';shutil.copy2(image,out/name)
            points[point]={'run':tag,'screenshot':name,'sha256':hashlib.sha256(image.read_bytes()).hexdigest()}
(out/'Acceptance.json').write_text(json.dumps({'runs':runs,'points':points,
    'boundary':'Production search/RPC with server position fixtures; not a continuous walking route or full source key-room restoration.'},
    ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('LOOT_EVIDENCE_ARCHIVED',len(runs),len(points))
