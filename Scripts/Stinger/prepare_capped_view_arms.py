"""Keep complete shoulder sleeves, close newly introduced cuts, preserve the source skin."""
from pathlib import Path
from collections import Counter,defaultdict
import ast,json,struct,math
project=Path(__file__).resolve().parents[2]
tree=ast.parse((project/'Scripts/Stinger/prepare_arms.py').read_text(encoding='utf-8'))
source=Path(next(n.value.args[0].value for n in tree.body if isinstance(n,ast.Assign) and n.targets[0].id=='source'))
raw=source.read_bytes();length=struct.unpack_from('<I',raw,12)[0]
doc=json.loads(raw[20:20+length]);binary=bytearray(raw[28+length:])
formats={5120:'b',5121:'B',5122:'h',5123:'H',5125:'I',5126:'f'}
sizes={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
def values(index):
    a=doc['accessors'][index];v=doc['bufferViews'][a['bufferView']]
    fmt='<'+formats[a['componentType']]*sizes[a['type']];start=v.get('byteOffset',0)+a.get('byteOffset',0)
    return [list(struct.unpack_from(fmt,binary,start+i*v.get('byteStride',struct.calcsize(fmt)))) for i in range(a['count'])]
def write(rows,kind,component=5126):
    while len(binary)%4:binary.append(0)
    start=len(binary);fmt='<'+formats[component]*sizes[kind]
    for row in rows:binary.extend(struct.pack(fmt,*row))
    view=len(doc['bufferViews']);doc['bufferViews'].append({'buffer':0,'byteOffset':start,'byteLength':len(binary)-start})
    a={'bufferView':view,'componentType':component,'count':len(rows),'type':kind}
    if kind=='VEC3':a.update(min=[min(r[i] for r in rows) for i in range(3)],max=[max(r[i] for r in rows) for i in range(3)])
    index=len(doc['accessors']);doc['accessors'].append(a);return index
report={'source_triangles':0,'view_triangles':0,'cap_triangles':0,'introduced_edges':0,'capped_edges':0,'remaining_open_edges':0,'boundaries':[]}
for node in doc['nodes']:
    if 'mesh' not in node or 'skin' not in node:continue
    skin=doc['skins'][node['skin']]
    arms={i for i,j in enumerate(skin['joints']) if doc['nodes'][j].get('name','').startswith(('LeftShoulder','RightShoulder','LeftArm','RightArm','LeftForeArm','RightForeArm','LeftHand','RightHand'))}
    mesh=doc['meshes'][node['mesh']];primitives=[]
    global_original=Counter();global_remaining=Counter();global_skin={};final_edges=Counter()
    def count_edges(ts,keys):
        return Counter(tuple(sorted((keys[t[i]],keys[t[(i+1)%3]]))) for t in ts for i in range(3) if keys[t[i]]!=keys[t[(i+1)%3]])
    for p in mesh['primitives']:
        ps=values(p['attributes']['POSITION']);js=values(p['attributes']['JOINTS_0']);ws=values(p['attributes']['WEIGHTS_0'])
        wa=doc['accessors'][p['attributes']['WEIGHTS_0']]
        if wa.get('normalized'):ws=[[x/{5121:255,5123:65535}[wa['componentType']] for x in row] for row in ws]
        ks=[tuple(round(v,5) for v in row) for row in ps]
        ix=[v[0] for v in values(p['indices'])];ts=[ix[i:i+3] for i in range(0,len(ix),3)]
        valid=[sum(w for j,w in zip(joint,weight) if j in arms)>=.5 for joint,weight in zip(js,ws)]
        global_original.update(count_edges(ts,ks));global_remaining.update(count_edges([t for t in ts if all(valid[i] for i in t)],ks))
        for k,j,w in zip(ks,js,ws):global_skin.setdefault(k,(j,w))
    global_adjacency=defaultdict(set)
    for (a,b),count in global_remaining.items():
        if count==1:global_adjacency[a].add(b);global_adjacency[b].add(a)
    for p in mesh['primitives']:
        data={name:values(index) for name,index in p['attributes'].items()}
        joints=data['JOINTS_0'];weights=data['WEIGHTS_0'];wa=doc['accessors'][p['attributes']['WEIGHTS_0']]
        if wa.get('normalized'):weights=[[x/{5121:255,5123:65535}[wa['componentType']] for x in row] for row in weights];data['WEIGHTS_0']=weights
        valid=[sum(w for j,w in zip(js,ws) if j in arms)>=.5 for js,ws in zip(joints,weights)]
        indices=[v[0] for v in values(p['indices'])];triangles=[indices[i:i+3] for i in range(0,len(indices),3)]
        report['source_triangles']+=len(triangles)
        selected=[t for t in triangles if all(valid[i] for i in t)]
        if not selected:continue
        positions=data['POSITION'];keys=[tuple(round(v,5) for v in row) for row in positions]
        def edges(ts):return Counter(tuple(sorted((keys[t[i]],keys[t[(i+1)%3]]))) for t in ts for i in range(3) if keys[t[i]]!=keys[t[(i+1)%3]])
        original=global_original;remaining=global_remaining
        boundary=[]
        for t in selected:
            for i in range(3):
                a,b=t[i],t[(i+1)%3];key=tuple(sorted((keys[a],keys[b])))
                if remaining[key]==1:boundary.append((a,b))
        adjacency=global_adjacency
        unseen={keys[i] for edge in boundary for i in edge};caps=[]
        while unseen:
            component=set();stack=[unseen.pop()]
            while stack:
                k=stack.pop()
                if k in component:continue
                component.add(k);unseen.discard(k);stack.extend(adjacency[k]-component)
            ring=[(a,b) for a,b in boundary if keys[a] in component];vertices=sorted({i for edge in ring for i in edge})
            center=len(positions)
            influences=defaultdict(float)
            for k in component:
                js,ws=global_skin[k]
                for j,w in zip(js,ws):influences[j]+=w/len(component)
            influences=sorted(influences.items(),key=lambda item:item[1],reverse=True)[:4]
            total=sum(w for _,w in influences);influences+=[(0,0)]*(4-len(influences))
            for name,rows in data.items():
                if name=='JOINTS_0':row=[j for j,_ in influences]
                elif name=='WEIGHTS_0':row=[w/total for _,w in influences]
                elif name=='POSITION':row=[sum(k[axis] for k in component)/len(component) for axis in range(3)]
                else:row=[sum(rows[i][axis] for i in vertices)/len(vertices) for axis in range(len(rows[0]))]
                if name=='NORMAL':
                    norm=math.sqrt(sum(v*v for v in row));row=[v/max(norm,1e-8) for v in row]
                rows.append(row)
            caps.extend([[b,a,center] for a,b in ring])
            ends=[k for k in component if len(adjacency[k])==1]
            if len(ends)==2:
                assert remaining[tuple(sorted(ends))]>=1, 'Open source seam needs a matching closing edge'
                a,b=(keys.index(k) for k in ends)
                caps.append([a,b,center])
            report['boundaries'].append({'edges':len(ring),'closed_loop':all(len(adjacency[k])==2 for k in component)})
        report['introduced_edges']+=sum(original[tuple(sorted((keys[a],keys[b])))]>=2 for a,b in boundary)
        report['capped_edges']+=len(caps);report['cap_triangles']+=len(caps)
        selected+=caps;report['view_triangles']+=len(selected)
        final_keys=[tuple(round(v,5) for v in row) for row in positions]
        final_edges.update(count_edges(selected,final_keys))
        for name,rows in data.items():
            old=doc['accessors'][p['attributes'][name]]
            component=5123 if name=='JOINTS_0' else 5126
            p['attributes'][name]=write(rows,old['type'],component)
        p['indices']=write([[i] for t in selected for i in t],'SCALAR',5125);primitives.append(p)
    mesh['primitives']=primitives;mesh['name']='Stinger_CappedViewArms';node['name']='Stinger_CappedViewArms'
    report['remaining_open_edges']+=sum(count==1 for count in final_edges.values())
assert report['view_triangles']>2000 and report['introduced_edges']<=report['capped_edges']
assert report['remaining_open_edges']==0, report
doc['buffers'][0]['byteLength']=len(binary)
encoded=json.dumps(doc,separators=(',',':')).encode();encoded+=b' '*(-len(encoded)%4);binary+=b'\0'*(-len(binary)%4)
output=project/'RawContent/Stinger/Stinger_CappedViewArms.glb'
output.write_bytes(struct.pack('<III',0x46546C67,2,28+len(encoded)+len(binary))+struct.pack('<II',len(encoded),0x4E4F534A)+encoded+struct.pack('<II',len(binary),0x004E4942)+binary)
report['strategy']='complete upper arms and shoulder sleeves; newly cut edges closed with skinned triangles'
(project/'Saved/Stinger/view_arms_topology.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
