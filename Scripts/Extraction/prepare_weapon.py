"""Offline stage of the generic weapon pipeline: resolve, extract and assemble.

    python -I Scripts/Extraction/prepare_weapon.py --weapon M4A1 [--source <archives dir>]

Run with the engine's bundled Python. Source archives stay read-only.

1. Resolve the official default preset: ``GunDescTable`` rows of ``preset`` give the
   part item ids and their parent nodes; ``CommonItemAssets_Weapon`` maps item ids to
   mesh names; each part's category prefix gives its mount socket (``CATEGORY_SOCKETS``).
2. Extract the part GLBs (1P/3P/UI), their textures, the listed animations and the
   ``WeaponPartsMeshSocketNameInfo`` rows into ``Saved/Extraction/Weapons/<id>/``.
3. Assemble one skinned GLB per view: parts are translated to their mount socket and
   bound to the receiver ``Root`` joint (the magazine to ``Mag_point``), preserving the
   receiver rig. Mount rotations must be identity; anything else is refused.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import posixpath
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import weapon_specs  # noqa: E402

VIEWS = ('1P', '3P')


def table(archive, suffix):
    names = [n for n in archive.namelist() if n.endswith(suffix)]
    assert len(names) == 1, (suffix, names)
    raw = archive.read(names[0])
    return json.loads(raw)['Rows'], names[0], hashlib.sha256(raw).hexdigest()


def resolve(source, spec):
    """Official default preset -> [(model, parent model, socket)] with the receiver first."""
    data = next(p for p in source.glob('*DataTables_001.zip') if 'Redirect' not in p.name)
    with zipfile.ZipFile(data) as archive:
        desc, desc_name, desc_hash = table(archive, '/WeaponPart/GunDescTable.json')
        items, items_name, items_hash = table(archive, '/CommonItemAsset/CommonItemAssets_Weapon.json')
        sockets, sockets_name, sockets_hash = table(archive, '/WeaponPart/WeaponPartsMeshSocketNameInfo.json')
    rows = [r for r in desc.values() if r['DesId'] == spec['preset']]
    assert rows, ('preset not found', spec['preset'])
    nodes = {}
    for r in rows:
        nodes.setdefault(r['Index'], r)
    assert nodes[1]['ItemId'] == spec['rec'], ('preset root is not the receiver', nodes[1]['ItemId'])

    def model(item):
        row = items.get(str(item)) or next((v for v in items.values() if str(v.get('ItemID')) == str(item)), None)
        assert row, ('no mesh for item', item)
        stem = row['Item1PMeshPath']['AssetPathName'].rsplit('/', 1)[-1].split('.')[0]
        assert stem.endswith('_1P'), stem
        return stem[:-3]

    parts = [(model(spec['rec']), None, None)]
    assert parts[0][0] == spec['receiver'], parts[0]
    for index in sorted(i for i in nodes if i != 1):
        node = nodes[index]
        category = str(node['ItemId'])[:4]
        socket = weapon_specs.CATEGORY_SOCKETS.get(category)
        assert socket, ('unsupported part category', node['ItemId'])
        parent = model(nodes[node['ParentIndex']]['ItemId'])
        parts.append((model(node['ItemId']), parent, socket))
    evidence = [dict(entry=n, sha256=h) for n, h in
                [(desc_name, desc_hash), (items_name, items_hash), (sockets_name, sockets_hash)]]
    return parts, sockets, evidence


def extract(source, out, spec, parts, socket_rows):
    records, models, animations, needed = [], {}, {}, set()
    wanted = {name for name, _, _ in parts}

    def save(archive, name):
        relative = PurePosixPath(name)
        assert not relative.is_absolute() and '..' not in relative.parts, name
        data = archive.read(name)
        target = out.joinpath(*relative.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            assert target.read_bytes() == data, 'Existing source differs: ' + str(target)
        else:
            target.write_bytes(data)
        records.append({'archive': Path(archive.filename).name, 'entry': name,
                        'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)})
        return data

    anim_targets = {role: '/Content/Models/Weapons/' + stem + '.psa' for role, stem in spec['anims'].items()}
    for path in sorted(source.glob('*Models_*.zip')):
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                if '/Models/Weapons/' not in name:
                    continue
                stem = Path(name).stem
                if name.endswith('.glb') and any(stem == m + '_' + v for m in wanted for v in VIEWS + ('UI',)):
                    data = save(archive, name)
                    magic, version, length, size, kind = struct.unpack_from('<5I', data)
                    assert magic == 0x46546C67 and version == 2 and kind == 0x4E4F534A
                    gltf = json.loads(data[20:20 + size])
                    models[stem] = name
                    for image in gltf.get('images', []):
                        uri = image.get('uri')
                        if uri and not uri.startswith('data:'):
                            needed.add(posixpath.normpath(posixpath.join(posixpath.dirname(name), uri)))
                for role, target in anim_targets.items():
                    if name.endswith(target) and role not in animations:
                        save(archive, name)
                        animations[role] = name
    found = set()
    for path in sorted(source.glob('*GenTexDir*.zip')):
        with zipfile.ZipFile(path) as archive:
            for name in needed.intersection(archive.namelist()):
                if name not in found:
                    save(archive, name)
                    found.add(name)
    assert needed == found, sorted(needed - found)
    for name in wanted:
        for view in VIEWS:
            assert name + '_' + view in models, ('model missing', name, view)
    missing = sorted(set(anim_targets) - set(animations))
    assert not missing, ('animations missing', missing)
    sockets = {}
    for name in models:
        matches = [(k, v) for k, v in socket_rows.items()
                   if k.startswith('/Game/Models/') and k.endswith('/' + name + '.' + name)]
        if len(matches) == 1:
            sockets[name] = {'path': matches[0][0], **matches[0][1]}
    return records, models, animations, sockets


def read(path):
    data = path.read_bytes()
    magic, version, length, count, kind = struct.unpack_from('<5I', data)
    assert magic == 0x46546C67 and version == 2 and length == len(data) and kind == 0x4E4F534A
    doc = json.loads(data[20:20 + count])
    offset = 20 + count
    size, kind = struct.unpack_from('<2I', data, offset)
    assert kind == 0x004E4942
    for node in doc['nodes']:
        if 'mesh' in node:
            assert not any(k in node for k in ('matrix', 'translation', 'rotation', 'scale')), path
    return doc, bytearray(data[offset + 8:offset + 8 + size])


def assemble(out, view, parts, models, sockets, weapon):
    receiver = parts[0][0]
    base = models[receiver + '_' + view]
    doc, binary = read(out / base)
    output = out / (weapon + '_Assembly_' + view + '.glb')
    translations = {receiver: (0, 0, 0)}

    def mount_offset(parent, socket):
        row = sockets[parent + '_' + view]
        index = row['SocketNames'].index(socket)
        assert max(abs(v) for v in row['SocketRotation'][index].values()) < .001, (parent, socket)
        p = row['SocketLocation'][index]
        local = (p['X'] / 100, p['Z'] / 100, p['Y'] / 100)
        return tuple(a + b for a, b in zip(translations[parent], local))

    def images(document, original):
        for image in document.get('images', []):
            uri = image.get('uri')
            if uri and not uri.startswith('data:'):
                image['uri'] = posixpath.normpath(posixpath.join(posixpath.dirname(original), uri))

    def append_bytes(data):
        binary.extend(b'\0' * (-len(binary) % 4))
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
    assert 'Root' in joints and 'Mag_point' in joints, sorted(joints)
    for part, parent, socket in parts[1:]:
        name = models[part + '_' + view]
        source, data = read(out / name)
        offset = translations[part] = mount_offset(parent, socket)
        positions = {p['attributes']['POSITION'] for m in source['meshes'] for p in m['primitives']}
        for index in positions:
            accessor = source['accessors'][index]
            assert accessor['componentType'] == 5126 and accessor['type'] == 'VEC3'
            bv = source['bufferViews'][accessor['bufferView']]
            start = bv.get('byteOffset', 0) + accessor.get('byteOffset', 0)
            stride = bv.get('byteStride', 12)
            for vertex in range(accessor['count']):
                address = start + vertex * stride
                xyz = struct.unpack_from('<3f', data, address)
                struct.pack_into('<3f', data, address, *(a + b for a, b in zip(xyz, offset)))
            for bound in ('min', 'max'):
                if bound in accessor:
                    accessor[bound] = [a + b for a, b in zip(accessor[bound], offset)]
        images(source, name)
        offsets = {k: len(doc.setdefault(k, [])) for k in
                   ('bufferViews', 'accessors', 'images', 'samplers', 'textures', 'materials', 'meshes')}
        buffer_offset = append_bytes(data)
        for item in source.get('bufferViews', []):
            item['buffer'] = 0
            item['byteOffset'] = item.get('byteOffset', 0) + buffer_offset
        for item in source.get('accessors', []):
            assert 'sparse' not in item
            if 'bufferView' in item:
                item['bufferView'] += offsets['bufferViews']
        for item in source.get('images', []):
            if 'bufferView' in item:
                item['bufferView'] += offsets['bufferViews']
        for item in source.get('textures', []):
            if 'source' in item:
                item['source'] += offsets['images']
            if 'sampler' in item:
                item['sampler'] += offsets['samplers']

        def remap_textures(value):
            if isinstance(value, dict):
                for key, child in value.items():
                    if key.endswith('Texture') and isinstance(child, dict) and 'index' in child:
                        child['index'] += offsets['textures']
                    else:
                        remap_textures(child)
            elif isinstance(value, list):
                for child in value:
                    remap_textures(child)

        for item in source.get('materials', []):
            remap_textures(item)
        for key in ('bufferViews', 'accessors', 'images', 'samplers', 'textures', 'materials'):
            doc[key].extend(source.get(key, []))
        for mesh in source['meshes']:
            for primitive in mesh['primitives']:
                assert not primitive.get('targets')
                attrs = primitive['attributes']
                count = source['accessors'][attrs['POSITION']]['count']
                for key in list(attrs):
                    if key.startswith(('JOINTS_', 'WEIGHTS_')):
                        del attrs[key]
                    else:
                        attrs[key] += offsets['accessors']
                joint = joints['Mag_point' if part.startswith('Mag_') else 'Root']
                attrs['JOINTS_0'] = attribute(struct.pack('<4H', joint, 0, 0, 0) * count, 5123, count, 'VEC4')
                attrs['WEIGHTS_0'] = attribute(struct.pack('<4f', 1, 0, 0, 0) * count, 5126, count, 'VEC4')
                if 'indices' in primitive:
                    primitive['indices'] += offsets['accessors']
                if 'material' in primitive:
                    primitive['material'] += offsets['materials']
            # One skinned mesh with material sections; Interchange would split separate mesh nodes.
            assert len(doc['meshes']) == 1
            doc['meshes'][0]['primitives'].extend(mesh['primitives'])
    doc['buffers'] = [{'byteLength': len(binary)}]
    encoded = json.dumps(doc, separators=(',', ':')).encode('utf-8')
    encoded += b' ' * (-len(encoded) % 4)
    binary.extend(b'\0' * (-len(binary) % 4))
    total = 12 + 8 + len(encoded) + 8 + len(binary)
    output.write_bytes(struct.pack('<5I', 0x46546C67, 2, total, len(encoded), 0x4E4F534A) + encoded
                       + struct.pack('<2I', len(binary), 0x004E4942) + binary)
    return output.name


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--weapon', required=True, choices=sorted(weapon_specs.WEAPONS))
    parser.add_argument('--source', default=str(weapon_specs.DEFAULT_SOURCE))
    options = parser.parse_args()
    spec = weapon_specs.WEAPONS[options.weapon]
    source = Path(options.source)
    out = weapon_specs.output_dir(ROOT, options.weapon)
    out.mkdir(parents=True, exist_ok=True)
    parts, socket_rows, evidence = resolve(source, spec)
    records, models, animations, sockets = extract(source, out, spec, parts, socket_rows)
    for name, parent, socket in parts[1:]:
        for view in VIEWS:
            row = sockets.get(parent + '_' + view)
            assert row and socket in row['SocketNames'], ('mount socket missing', parent, view, socket)
    (out / 'AssemblySockets.json').write_text(json.dumps(sockets, indent=2) + '\n', encoding='utf-8')
    assemblies = {view: assemble(out, view, parts, models, sockets, options.weapon) for view in VIEWS}
    manifest = {'schema': 1, 'weapon': options.weapon, 'rec': spec['rec'], 'preset': spec['preset'],
                'parts': [{'model': m, 'parent': p, 'socket': s} for m, p, s in parts],
                'models': models, 'animations': animations, 'assemblies': assemblies,
                'tables': evidence, 'files': records}
    (out / 'Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'weapon': options.weapon, 'parts': [m for m, _, _ in parts],
                      'animations': sorted(animations), 'assemblies': assemblies}, ensure_ascii=False))


if __name__ == '__main__':
    main()
