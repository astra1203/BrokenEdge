"""Build a provisional SCAR preview from source-space parts, preserving the receiver rig.

All parts must have identity mesh-node transforms. Rigid pieces follow the receiver
root; the magazine follows Mag_point. Source GLBs remain unchanged. This does not
establish the official default assembly or the final grip/muzzle calibration.
"""
import json
from pathlib import Path
import posixpath
import struct

ROOT = Path(__file__).resolve().parents[2] / 'Saved/Extraction/SpaceCenterExpansionSources01'
PARTS = ['Bar_M-400_scarH_055', 'Han_ScarH_std_066', 'Mag_76251-20_scarh_114',
         'Sto_A_ScarH_065', 'FroS_ScarH_015', 'ReaS_ScarH_024', 'PisG_B_M4A1-Std_001']
MOUNTS = [('Rec_AR_ScarH_045', 'Bar_point'), ('Rec_AR_ScarH_045', 'Han_point'),
          ('Rec_AR_ScarH_045', 'Mag_point'), ('Rec_AR_ScarH_045', 'Sto_point'),
          ('Bar_M-400_scarH_055', 'FSig_point'), ('Rec_AR_ScarH_045', 'Rea_point'),
          ('Rec_AR_ScarH_045', 'PisG_point')]


def read(path):
    data = path.read_bytes()
    magic, version, length, count, kind = struct.unpack_from('<5I', data)
    assert magic == 0x46546C67 and version == 2 and length == len(data) and kind == 0x4E4F534A
    doc = json.loads(data[20:20+count])
    offset = 20+count
    size, kind = struct.unpack_from('<2I', data, offset)
    assert kind == 0x004E4942
    for node in doc['nodes']:
        if 'mesh' in node:
            assert not any(k in node for k in ('matrix', 'translation', 'rotation', 'scale'))
    return doc, bytearray(data[offset+8:offset+8+size])


def assemble(view, manifest):
    base = manifest['models']['Rec_AR_ScarH_045_'+view]
    doc, binary = read(ROOT/base)
    output = ROOT/('SCARH_Candidate_'+view+'.glb')
    sockets = json.loads((ROOT/'AssemblySockets.json').read_text(encoding='utf-8'))
    translations = {'Rec_AR_ScarH_045': (0, 0, 0)}

    def mount_offset(parent, socket):
        row = sockets[parent+'_'+view]
        index = row['SocketNames'].index(socket)
        # These basic mounts have identity rotation to source export precision.
        # Refuse other attachments rather than silently misplacing them.
        assert max(abs(v) for v in row['SocketRotation'][index].values()) < .001
        p = row['SocketLocation'][index]
        local = (p['X']/100, p['Z']/100, p['Y']/100)
        return tuple(a+b for a,b in zip(translations[parent], local))

    def images(document, original):
        for image in document.get('images', []):
            uri = image.get('uri')
            if uri and not uri.startswith('data:'):
                image['uri'] = posixpath.normpath(posixpath.join(posixpath.dirname(original), uri))

    def append_bytes(data):
        binary.extend(b'\0'*(-len(binary) % 4))
        offset = len(binary)
        binary.extend(data)
        return offset

    def attribute(data, component, count, kind):
        offset = append_bytes(data)
        bv = len(doc['bufferViews'])
        doc['bufferViews'].append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(data)})
        index = len(doc['accessors'])
        doc['accessors'].append({'bufferView': bv, 'componentType': component, 'count': count, 'type': kind})
        return index

    images(doc, base)
    skin = doc['skins'][0]
    joints = {doc['nodes'][node]['name']: index for index, node in enumerate(skin['joints'])}
    assert 'Root' in joints and 'Mag_point' in joints
    for part, (parent, socket) in zip(PARTS, MOUNTS):
        name = manifest['models'][part+'_'+view]
        source, data = read(ROOT/name)
        offset = translations[part] = mount_offset(parent, socket)
        positions = {p['attributes']['POSITION'] for m in source['meshes'] for p in m['primitives']}
        for index in positions:
            accessor = source['accessors'][index]
            assert accessor['componentType'] == 5126 and accessor['type'] == 'VEC3'
            bv = source['bufferViews'][accessor['bufferView']]
            start = bv.get('byteOffset',0)+accessor.get('byteOffset',0)
            stride = bv.get('byteStride',12)
            for vertex in range(accessor['count']):
                address = start+vertex*stride
                xyz = struct.unpack_from('<3f', data, address)
                struct.pack_into('<3f', data, address, *(a+b for a,b in zip(xyz,offset)))
            for bound in ('min','max'):
                if bound in accessor: accessor[bound] = [a+b for a,b in zip(accessor[bound],offset)]
        images(source, name)
        offsets = {k: len(doc.setdefault(k, [])) for k in ('bufferViews','accessors','images','samplers','textures','materials','meshes')}
        buffer_offset = append_bytes(data)
        for item in source.get('bufferViews', []):
            item['buffer'] = 0
            item['byteOffset'] = item.get('byteOffset',0)+buffer_offset
        for item in source.get('accessors', []):
            assert 'sparse' not in item
            if 'bufferView' in item: item['bufferView'] += offsets['bufferViews']
        for item in source.get('images', []):
            if 'bufferView' in item: item['bufferView'] += offsets['bufferViews']
        for item in source.get('textures', []):
            if 'source' in item: item['source'] += offsets['images']
            if 'sampler' in item: item['sampler'] += offsets['samplers']

        def remap_textures(value):
            if isinstance(value,dict):
                for key, child in value.items():
                    if key.endswith('Texture') and isinstance(child,dict) and 'index' in child:
                        child['index'] += offsets['textures']
                    else: remap_textures(child)
            elif isinstance(value,list):
                for child in value: remap_textures(child)

        for item in source.get('materials', []): remap_textures(item)
        for key in ('bufferViews','accessors','images','samplers','textures','materials'):
            doc[key].extend(source.get(key, []))
        for mesh in source['meshes']:
            for primitive in mesh['primitives']:
                assert not primitive.get('targets')
                attrs = primitive['attributes']
                count = source['accessors'][attrs['POSITION']]['count']
                for key in list(attrs):
                    if key.startswith(('JOINTS_','WEIGHTS_')): del attrs[key]
                    else: attrs[key] += offsets['accessors']
                joint = joints['Mag_point' if part.startswith('Mag_') else 'Root']
                attrs['JOINTS_0'] = attribute(struct.pack('<4H',joint,0,0,0)*count,5123,count,'VEC4')
                attrs['WEIGHTS_0'] = attribute(struct.pack('<4f',1,0,0,0)*count,5126,count,'VEC4')
                if 'indices' in primitive: primitive['indices'] += offsets['accessors']
                if 'material' in primitive: primitive['material'] += offsets['materials']
            # One skinned mesh with material sections, rather than separate mesh
            # nodes which Interchange intentionally imports as separate assets.
            assert len(doc['meshes']) == 1
            doc['meshes'][0]['primitives'].extend(mesh['primitives'])
    doc['buffers'] = [{'byteLength': len(binary)}]
    encoded = json.dumps(doc,separators=(',',':')).encode('utf-8')
    encoded += b' '*(-len(encoded)%4)
    binary.extend(b'\0'*(-len(binary)%4))
    total = 12+8+len(encoded)+8+len(binary)
    output.write_bytes(struct.pack('<5I',0x46546C67,2,total,len(encoded),0x4E4F534A)+encoded
                       +struct.pack('<2I',len(binary),0x004E4942)+binary)
    return str(output)


if __name__ == '__main__':
    manifest=json.loads((ROOT/'Manifest.json').read_text(encoding='utf-8'))
    print(json.dumps([assemble(view,manifest) for view in ('1P','3P')]))
