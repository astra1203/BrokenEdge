"""Measure recorded wrist/ankle positions against fixture or bound source rungs."""
import argparse
import json
import math
from pathlib import Path
import re

parser=argparse.ArgumentParser()
parser.add_argument('directory',type=Path)
parser.add_argument('--formal',action='store_true')
args=parser.parse_args()
folder=args.directory.resolve()
root=Path(__file__).resolve().parents[2]
if args.formal:
    route=json.loads((root/'Scripts/Traversal/formal_route.json').read_text(encoding='utf-8'))
    bindings=json.loads((root/'Docs/Extraction/Traversal/Evidence/LaddersReport.json').read_text(encoding='utf-8'))
    ladder=next(p for p in bindings['candidates'] if p['id']==route['ladder']['source_id'])
    geometry=json.loads((root/'Docs/Extraction/Traversal/Evidence/SourceRungs.json').read_text(encoding='utf-8'))[ladder['mesh']]
    half_width=min(22.,min((r['max'][1]-r['min'][1])/2-6 for r in geometry['rungs']))
    rungs=ladder['contact_rungs']
    facing=ladder['binding_forward']
else:
    half_width=22
    rungs=[[29995,29100,6016.5+i*25] for i in range(10)]
    facing=[1,0,0]
right=[-facing[1],facing[0],0]
result={}
for peer in ('A','B'):
    source=folder/(peer+('-formal-ladder-attached' if args.formal else '-ladder-paused')+'-pose.json')
    pose=json.loads(source.read_text(encoding='utf-8'))
    meshes={}
    for mesh in ('world','first_person'):
        if mesh not in pose:continue
        bones={}
        for name in ('LeftHand','RightHand','LeftFoot','RightFoot'):
            if mesh=='first_person' and name.endswith('Foot'):continue
            text=pose[mesh][name]['world_position']
            p=[float(v) for v in re.findall(r'[XYZ]=(-?[\d.]+)',text)]
            foot=name.endswith('Foot')
            lateral=half_width*(.75 if foot else 1)*(1 if name.startswith('Right') else -1)
            targets=[[r[i]+right[i]*lateral-facing[i]*(6 if foot else 0) for i in range(3)] for r in rungs]
            nearest=min(targets,key=lambda q:math.dist(p,q))
            bones[name]=dict(position=p,target=nearest,error_cm=math.dist(p,nearest))
        meshes[mesh]=bones
    result[peer]=meshes
report=dict(result='measured',poses=result,boundary='Wrist and ankle targets at a paused sample. A limb in the reach part of its cycle may be between rungs; this does not replace video inspection of transitions and fingers/soles.')
(folder/('FormalContactAnalysis.json' if args.formal else 'ContactAnalysis.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
