"""保留原 GLB 蒙皮、骨架与贴图，只留下双臂及双手控制的三角面。"""
from pathlib import Path
import json, struct

project=Path(__file__).resolve().parents[2]
source=Path(r'D:\司晨\源代码和素材\蜂医的人物模型和相关动画资产\01_完整人物模型\可直接导入\Stinger_3P_Textured.glb')
raw=source.read_bytes()
json_length=struct.unpack_from('<I',raw,12)[0]
doc=json.loads(raw[20:20+json_length])
binary=bytearray(raw[28+json_length:])
formats={5120:'b',5121:'B',5122:'h',5123:'H',5125:'I',5126:'f'}
sizes={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}

def values(index):
    accessor=doc['accessors'][index]; view=doc['bufferViews'][accessor['bufferView']]
    fmt='<'+formats[accessor['componentType']]*sizes[accessor['type']]
    stride=view.get('byteStride',struct.calcsize(fmt))
    start=view.get('byteOffset',0)+accessor.get('byteOffset',0)
    result=[struct.unpack_from(fmt,binary,start+i*stride) for i in range(accessor['count'])]
    if accessor.get('normalized'):
        maximum={5121:255,5123:65535}[accessor['componentType']]
        result=[tuple(x/maximum for x in value) for value in result]
    return result

kept=0
for node in doc['nodes']:
    if 'mesh' not in node or 'skin' not in node: continue
    skin=doc['skins'][node['skin']]
    arm_joints={i for i,joint in enumerate(skin['joints']) if doc['nodes'][joint].get('name','').startswith(('LeftForeArm','LeftHand','RightForeArm','RightHand'))}
    mesh=doc['meshes'][node['mesh']]
    primitives=[]
    for primitive in mesh['primitives']:
        joints=values(primitive['attributes']['JOINTS_0'])
        weights=values(primitive['attributes']['WEIGHTS_0'])
        valid=[sum(w for j,w in zip(js,ws) if j in arm_joints)>=.55 for js,ws in zip(joints,weights)]
        indices=[v[0] for v in values(primitive['indices'])]
        selected=[v for i in range(0,len(indices),3) if all(valid[v] for v in indices[i:i+3]) for v in indices[i:i+3]]
        if not selected: continue
        while len(binary)%4: binary.append(0)
        start=len(binary)
        binary.extend(struct.pack('<'+'I'*len(selected),*selected))
        view=len(doc['bufferViews'])
        doc['bufferViews'].append({'buffer':0,'byteOffset':start,'byteLength':len(selected)*4,'target':34963})
        accessor=len(doc['accessors'])
        doc['accessors'].append({'bufferView':view,'componentType':5125,'count':len(selected),'type':'SCALAR','min':[min(selected)],'max':[max(selected)]})
        primitive['indices']=accessor
        primitives.append(primitive)
        kept+=len(selected)//3
    mesh['primitives']=primitives
    mesh['name']='Stinger_1P_Forearms'
    node['name']='Stinger_1P_Forearms'
assert kept>1000,kept
doc['buffers'][0]['byteLength']=len(binary)
encoded=json.dumps(doc,separators=(',',':')).encode()
encoded+=b' '*(-len(encoded)%4)
binary+=b'\0'*(-len(binary)%4)
output=project/'RawContent'/'Stinger'/'Stinger_1P_Forearms.glb'
output.parent.mkdir(parents=True,exist_ok=True)
output.write_bytes(struct.pack('<III',0x46546C67,2,28+len(encoded)+len(binary))+struct.pack('<II',len(encoded),0x4E4F534A)+encoded+struct.pack('<II',len(binary),0x004E4942)+binary)
print('ARMS_TRIANGLES',kept,'OUTPUT',output)
