"""Archive verified safe coordinates and extract only their source meshes/textures."""
import hashlib
import json
import posixpath
import struct
import zipfile
from pathlib import Path, PurePosixPath

root=Path(__file__).resolve().parents[2]
archives=Path('D:/司晨/源代码和素材/三角洲解包资产/网盘上传压缩包')
work=root/'Saved/Extraction/LootSources'
work.mkdir(parents=True,exist_ok=True)
source=root/'Saved/Extraction/restoration-loot-source02/Decoded/Packages.json'
packages=json.loads(source.read_text(encoding='utf-8'))
types={'BP_Interactor_Container_SafeBox_C':('LargeSafe','大保险柜','props_collect_box_546_3P'),
       'BP_Interact_SmallSafebox_C':('SmallSafe','小保险箱','props_collect_SmallSafeBox_802_3P')}
points=[]
for package in packages:
    objects={o['export_index']:o for o in package['objects']}
    for obj in objects.values():
        if obj['Type'] not in types:continue
        props=obj['Properties'];ref=props['RootComponent']['ObjectPath']
        component=objects[int(ref.rsplit('.',1)[-1])]['Properties']
        assert not component.get('AttachParent'),obj['Name']
        kind,label,model=types[obj['Type']]
        position=component['RelativeLocation']
        points.append({'id':hashlib.sha256((package['package']+':'+obj['Name']).encode()).hexdigest()[:20],
            'variant':package['package'].split('/')[3], 'kind':kind,'label':label,'model':model,
            'package':package['package'],'actor':obj['Name'],'export_index':obj['export_index'],
            'room':props.get('地图小区域自定义名称',''),'source_identity':props.get('唯一性',''),
            'source_position':position,'source_rotation':component.get('RelativeRotation',{}),
            'source_scale':component.get('RelativeScale3D',{'X':1,'Y':1,'Z':1}),
            'world_position':{k:position[k]+v for k,v in zip(('X','Y','Z'),(-664000,451000,22400))},
            'group':'','group_evidence':'unresolved; not inferred from proximity',
            'point_evidence':'serialized actor root; runtime floor/access validation pending'})
assert len(points)==36,len(points)
models={};records=[];needed=set()
def save(z,name):
    relative=PurePosixPath(name)
    assert not relative.is_absolute() and '..' not in relative.parts
    data=z.read(name);path=work.joinpath(*relative.parts);path.parent.mkdir(parents=True,exist_ok=True)
    if path.exists(): assert path.read_bytes()==data
    else:path.write_bytes(data)
    records.append({'archive':Path(z.filename).name,'entry':name,'sha256':hashlib.sha256(data).hexdigest()})
    return data
for archive in sorted(archives.glob('*Models_*.zip')):
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            model=next((t[2] for t in types.values() if name.endswith('/'+t[2]+'.glb')),None)
            if not model:continue
            data=save(z,name);gltf=json.loads(data[20:20+struct.unpack_from('<I',data,12)[0]])
            models[model]=name
            for img in gltf.get('images',[]):
                uri=img.get('uri','')
                if uri and not uri.startswith('data:'):needed.add(posixpath.normpath(posixpath.join(posixpath.dirname(name),uri)))
found=set()
for archive in sorted(archives.glob('*GenTexDir*.zip')):
    with zipfile.ZipFile(archive) as z:
        for name in needed.intersection(z.namelist()):
            if name not in found:save(z,name);found.add(name)
assert len(models)==2,models
assert found==needed,needed-found
report={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'points':points,
        'models':models,'files':records,'random_group_status':'unresolved','drop_policy':'project compatibility'}
(work/'Manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
out=root/'Artifacts/Extraction/RestorationEvidence/Loot';out.mkdir(parents=True,exist_ok=True)
(out/'SourcePoints.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('LOOT_SOURCES_READY',len(points),len(models),len(found))
