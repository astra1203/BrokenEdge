"""Read actual restored ladder vertices to calibrate rung contacts."""
import json
import os
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
out=Path(os.environ['TRAVERSAL_EDITOR_OUT'])
sources=json.loads((root/'Saved/Traversal/Sources.json').read_text(encoding='utf-8'))
native=json.loads((root/'Saved/Traversal/SourceCorrection/NativeAssets.json').read_text(encoding='utf-8'))
meshes={}
for path in sorted(sources['meshes']):
    project_path=native[path]['asset']
    asset=unreal.load_asset(project_path)
    assert asset,project_path
    vertices=json.loads(unreal.StingerAssetLibrary.describe_ladder_vertices(asset))
    assert vertices,path
    low=[min(p[i] for p in vertices) for i in range(3)]
    high=[max(p[i] for p in vertices) for i in range(3)]
    center=(low[0]+high[0])/2
    central=sorted((p for p in vertices if abs(p[0]-center)<(high[0]-low[0])*.4),key=lambda p:p[2])
    groups=[]
    for point in central:
        if not groups or point[2]-groups[-1][-1][2]>4:groups.append([])
        groups[-1].append(point)
    bands=[]
    for points in groups:
        bands.append(dict(count=len(points),center=[sum(p[i] for p in points)/len(points) for i in range(3)],
                          min=[min(p[i] for p in points) for i in range(3)],max=[max(p[i] for p in points) for i in range(3)]))
    meshes[path]=dict(project_asset=project_path,vertices=len(vertices),min=low,max=high,bands=bands)
    bounds=sources['meshes'][path]['source_mesh']['Properties']['ExtendedBounds']
    assert max(abs(actual-(bounds['Origin'][axis]+sign*bounds['BoxExtent'][axis]))
               for values,sign in ((low,-1),(high,1)) for axis,actual in zip('XYZ',values)) < 1., 'Native import changed source axes/bounds'
(out/'Report.json').write_text(json.dumps(dict(result='passed',meshes=meshes),indent=2)+'\n',encoding='utf-8')
