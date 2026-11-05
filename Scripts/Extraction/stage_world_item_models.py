"""Prepare rigid GLBs with explicit material bindings from converter JSON, keeping source bytes intact."""
import hashlib
import json
from pathlib import Path
import shutil
import struct

ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/'Saved/Extraction/WorldModels'
SOURCE=WORK/'Converted'
DEST=WORK/'Import'
DEST.mkdir(exist_ok=True)
selection=json.loads((WORK/'Selection.json').read_text(encoding='utf-8'))
materials={}
for path in SOURCE.rglob('*.json'):
    if path.name in ['Packages.json','ExportResults.json']:continue
    data=json.loads(path.read_text(encoding='utf-8'))
    if 'Textures' in data: materials.setdefault(path.stem.casefold(),[]).append((path,data))
report={'models':{},'materials_without_color_texture':[]}

for package in selection['packages']:
    path=SOURCE/(package.replace('/Game/','DeltaForce/Content/')+'.glb')
    assert path.exists(),path
    data=path.read_bytes()
    assert struct.unpack_from('<I',data)[0]==0x46546c67
    length=struct.unpack_from('<I',data,12)[0]
    gltf=json.loads(data[20:20+length])
    tail=data[20+length:]
    tag='W_'+hashlib.sha256(package.encode()).hexdigest()[:16]
    gltf.pop('skins',None)
    for node in gltf['nodes']:node.pop('skin',None)
    for mesh in gltf['meshes']:
        mesh['name']=tag
        for primitive in mesh['primitives']:
            for attr in list(primitive['attributes']):
                if attr.startswith(('JOINTS_','WEIGHTS_')):del primitive['attributes'][attr]
    gltf['images']=[];gltf['textures']=[]
    def texture(reference):
        source=SOURCE/(reference['ObjectPath'].rsplit('.',1)[0]+'.png')
        assert source.exists(),source
        filename='T_'+hashlib.sha256(source.relative_to(SOURCE).as_posix().encode()).hexdigest()[:16]+'.png'
        if not (DEST/filename).exists():shutil.copy2(source,DEST/filename)
        index=len(gltf['images'])
        gltf['images'].append({'uri':filename})
        gltf['textures'].append({'source':index})
        return {'index':index}
    for index,mat in enumerate(gltf.get('materials',[])):
        name=mat['name']
        candidates=materials.get(name.casefold(),[])
        # Resolve same-named materials using the owning model folder before global names.
        local=[(p,m) for p,m in candidates if p.is_relative_to(path.parent)]
        if local:candidates=local
        source=candidates[0][1] if len(candidates)==1 else {}
        textures=source.get('Textures',{})
        color=next((textures[k] for k in ['BaseColorMap','PM_Diffuse','Diffuse'] if k in textures),None)
        pbr={'roughnessFactor':.55,'metallicFactor':0}
        if color:pbr['baseColorTexture']=texture(color)
        else:report['materials_without_color_texture'].append(dict(package=package,material=name,candidates=len(candidates)))
        mat.clear();mat.update(name=tag+'_M'+str(index),pbrMetallicRoughness=pbr,doubleSided=True)
        # Packed NRM carries roughness/metalness, so it is not a glTF tangent normal.
        if 'NormalMap' in textures:mat['normalTexture']=texture(textures['NormalMap'])
    encoded=json.dumps(gltf,separators=(',',':')).encode()
    encoded+=b' '*((-len(encoded))%4)
    output=struct.pack('<III',0x46546c67,2,20+len(encoded)+len(tail))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+tail
    target=DEST/(tag+'.glb');target.write_bytes(output)
    report['models'][package]=dict(file=str(target),name=tag,sha256=hashlib.sha256(output).hexdigest(),
                                  source_sha256=hashlib.sha256(data).hexdigest())
(WORK/'ImportManifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('WORLD_ITEM_MODELS_STAGED',len(report['models']),'untextured materials',len(report['materials_without_color_texture']))
