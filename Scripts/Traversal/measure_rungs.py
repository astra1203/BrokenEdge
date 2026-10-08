"""Derive contact surfaces from the audited source GLB triangle geometry."""
import hashlib
import json
from pathlib import Path
import statistics
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts/SpaceCenter'))
from audit_geometry import glb,accessor

sources=json.loads((ROOT/'Saved/Traversal/Sources.json').read_text(encoding='utf-8'))
result={}
for path in sorted(set(row['mesh'] for row in sources['ladders'])):
    entry=sources['meshes'][path]
    filename=Path(entry['file'])
    doc,binary=glb(filename)
    faces=[]
    for mesh in doc['meshes']:
        for primitive in mesh['primitives']:
            vertices=[(p[0]*100,p[2]*100,p[1]*100) for p in accessor(doc,binary,primitive['attributes']['POSITION'])]
            indices=[p[0] for p in accessor(doc,binary,primitive['indices'])]
            for i in range(0,len(indices),3):
                triangle=[vertices[j] for j in indices[i:i+3]]
                low=[min(p[k] for p in triangle) for k in range(3)]
                high=[max(p[k] for p in triangle) for k in range(3)]
                if high[1]-low[1]>35 and high[0]-low[0]>3 and high[2]-low[2]<4:
                    faces.append(dict(min=low,max=high,z=(low[2]+high[2])/2))
    groups=[]
    for face in sorted(faces,key=lambda p:p['z']):
        if not groups or face['z']-groups[-1][-1]['z']>15:groups.append([])
        groups[-1].append(face)
    rungs=[max(group,key=lambda p:p['z']) for group in groups]
    assert len(rungs)>=4,(path,rungs)
    result[path]=dict(source_sha256=hashlib.sha256(filename.read_bytes()).hexdigest(),rungs=rungs,
                      median_spacing=statistics.median(b['z']-a['z'] for a,b in zip(rungs,rungs[1:])))
(ROOT/'Saved/Traversal/Rungs.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:dict(heights=[round(r['z'],2) for r in v['rungs']],spacing=v['median_spacing']) for k,v in result.items()}))
