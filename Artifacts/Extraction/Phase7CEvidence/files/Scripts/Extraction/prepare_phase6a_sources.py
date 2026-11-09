"""Extract only the approved original attachments and their texture dependencies.

Source ZIPs remain read-only. Outputs and hash manifest live in Saved; UE import is
performed separately in an isolated editor after compiling the native definitions.
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import posixpath
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('D:/司晨/源代码和素材/三角洲解包资产/网盘上传压缩包')
OUT = ROOT / 'Saved/Extraction/Phase6ASources'
CHOICES = {'RedDot': 'Sco_Re_Reddot_035', 'VerticalGrip': 'FroG_Pic_Rvg_020',
           'Compensator': 'Muz_M-C_ARCompensator_081'}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    records, wanted = [], set()

    def save(z, path):
        relative = PurePosixPath(path)
        assert not relative.is_absolute() and '..' not in relative.parts
        data = z.read(path)
        target = OUT.joinpath(*relative.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
        records.append({'archive': Path(z.filename).name, 'entry': path,
                        'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)})
        return data

    selected, rigid, derived = {}, {}, []
    for archive in sorted(SOURCE.glob('*Models_*.zip')):
        with zipfile.ZipFile(archive) as z:
            for path in z.namelist():
                for item, model in CHOICES.items():
                    for view in ('1P', '3P'):
                        if path.endswith('/' + model + '_' + view + '.glb'):
                            data = save(z, path)
                            magic, version, size, chunk_size, chunk_kind = struct.unpack_from('<5I', data)
                            assert magic == 0x46546C67 and version == 2 and size == len(data) and chunk_kind == 0x4E4F534A
                            gltf = json.loads(data[20:20 + chunk_size])
                            assert gltf.get('meshes')
                            assert all(not any(k in node for k in ('translation', 'rotation', 'scale', 'matrix'))
                                       for node in gltf['nodes'] if 'mesh' in node), 'Mesh transform requires baking'
                            selected[item + '_' + view] = path
                            # These are rigid attachment meshes exported with a trivial skin.
                            # Keep original vertex buffers and material slots; remove only the rig.
                            for mesh in gltf['meshes']:
                                for primitive in mesh['primitives']:
                                    for attribute in list(primitive['attributes']):
                                        if attribute.startswith(('JOINTS_', 'WEIGHTS_')):
                                            del primitive['attributes'][attribute]
                            gltf.pop('skins', None)
                            gltf.pop('animations', None)
                            gltf['nodes'] = [{'mesh': n, 'name': model} for n in range(len(gltf['meshes']))]
                            gltf['scenes'] = [{'nodes': list(range(len(gltf['nodes'])))}]
                            gltf['scene'] = 0
                            encoded = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
                            encoded += b' ' * (-len(encoded) % 4)
                            tail = data[20 + chunk_size:]
                            converted = struct.pack('<5I', magic, 2, 20 + len(encoded) + len(tail), len(encoded), chunk_kind) + encoded + tail
                            rigid_path = path[:-4] + '_Rigid.glb'
                            OUT.joinpath(*PurePosixPath(rigid_path).parts).write_bytes(converted)
                            rigid[item + '_' + view] = rigid_path
                            derived.append({'entry': rigid_path, 'source': path, 'bytes': len(converted),
                                            'sha256': hashlib.sha256(converted).hexdigest()})
                            for image in gltf.get('images', []):
                                if image.get('uri'):
                                    wanted.add(posixpath.normpath(posixpath.join(posixpath.dirname(path), image['uri'])))
    assert len(selected) == 6, selected
    found = set()
    for archive in sorted(SOURCE.glob('*GenTexDirHD_*.zip')):
        with zipfile.ZipFile(archive) as z:
            for path in wanted.intersection(z.namelist()):
                assert save(z, path).startswith(b'\x89PNG\r\n\x1a\n')
                found.add(path)
    assert found == wanted, wanted - found
    manifest = {'models': selected, 'rigid_models': rigid, 'files': records, 'derived_files': derived, 'source_root': str(SOURCE),
                'result': 'source-validated', 'note': 'UE import and visual acceptance are separate.'}
    (OUT / 'Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'models': len(selected), 'textures': len(found), 'result': manifest['result']}))


if __name__ == '__main__':
    main()
