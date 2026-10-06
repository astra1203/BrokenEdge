"""Trace each native terrain tile in PIE against deterministic source triangle samples."""
import json
import math
import struct
from pathlib import Path
import unreal


def audit(world,manifest):
    actors={a.get_actor_label():a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.StaticMeshActor)}
    probes=[];errors=[]
    for tile in manifest['terrain']['tiles']:
        actor=actors.get(tile['id'])
        if not actor:raise RuntimeError('Missing PIE terrain actor: '+tile['id'])
        data=Path(tile['file']).read_bytes();length=struct.unpack_from('<I',data,12)[0]
        doc=json.loads(data[20:20+length]);binary=28+length
        primitive=doc['meshes'][0]['primitives'][0]
        def accessor(index,fmt):
            entry=doc['accessors'][index];view=doc['bufferViews'][entry['bufferView']]
            start=binary+view.get('byteOffset',0)+entry.get('byteOffset',0);stride=view.get('byteStride',struct.calcsize(fmt))
            return [struct.unpack_from(fmt,data,start+i*stride) for i in range(entry['count'])]
        positions=accessor(primitive['attributes']['POSITION'],'<3f')
        indices=[v[0] for v in accessor(primitive['indices'],'<I')]
        count=len(indices)//3
        # Interior and boundary-adjacent triangles exercise every tile's native collision.
        selected=sorted({0,1,count//4,count//2,3*count//4,count-2,count-1})
        for index in selected:
            triangle=[positions[v] for v in indices[index*3:index*3+3]]
            expected=[sum(p[axis]*100 for p in triangle)/3+tile['position'][i] for i,axis in enumerate([0,2,1])]
            start=unreal.Vector(expected[0],expected[1],expected[2]+1000)
            end=unreal.Vector(expected[0],expected[1],expected[2]-1000)
            hit=actor.static_mesh_component.line_trace_component(start,end,True,False,False)
            if not hit:
                errors.append({'tile':tile['id'],'triangle':index,'error':'no_native_collision'});continue
            values=hit if isinstance(hit,tuple) else hit.to_tuple()
            vectors=[v for v in values if isinstance(v,unreal.Vector)]
            if len(vectors)<2:raise RuntimeError('Unexpected component trace result: '+repr(hit))
            point,normal=vectors[:2];actual=[point.x,point.y,point.z]
            error=math.dist(expected,actual)
            row={'tile':tile['id'],'triangle':index,'source':expected,'native':actual,'error_cm':error,'normal_z':normal.z}
            probes.append(row)
            if error>1 or normal.z<=0:errors.append(row)
    return {'result':'failed' if errors else 'passed','tiles':len(manifest['terrain']['tiles']),
            'probes':probes,'errors':errors,'max_error_cm':max((p['error_cm'] for p in probes),default=0.)}
