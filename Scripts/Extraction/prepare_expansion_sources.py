"""Extract selected basic SCAR-H/straight-knife candidates and dependencies.

The selected SCAR parts are a provisional assembly, not a verified official preset.
Source archives stay read-only; original paths and hashes are retained in Saved.
"""
import hashlib
import argparse
import json
from pathlib import Path, PurePosixPath
import posixpath
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('D:/司晨/源代码和素材/三角洲解包资产/网盘上传压缩包')
OUT = ROOT / 'Saved/Extraction/SpaceCenterExpansionSources01'
MODELS = ['Rec_AR_ScarH_045', 'Bar_M-400_scarH_055', 'Han_ScarH_std_066',
          'Mag_76251-20_scarh_114', 'Sto_A_ScarH_065', 'FroS_ScarH_015',
          'ReaS_ScarH_024', 'PisG_B_M4A1-Std_001', 'Knife_FairbairnSykes_001']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--resume', action='store_true', help='Add missing candidates without replacing extracted source files')
    options = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=options.resume)
    previous = json.loads((OUT/'Manifest.json').read_text(encoding='utf-8')) if options.resume else {}
    records = previous.get('files', [])
    models = previous.get('models', {})
    animations = previous.get('animations', [])
    needed = set()

    def save(archive, name):
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Unsafe source path')
        data = archive.read(name)
        target = OUT.joinpath(*relative.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            assert target.read_bytes() == data, 'Existing source differs: '+str(target)
            return data
        target.write_bytes(data)
        records.append({'archive': Path(archive.filename).name, 'entry': name,
                        'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)})
        return data

    for path in sorted(SOURCE.glob('*Models_*.zip')):
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                if '/Models/Weapons/' not in name:
                    continue
                match = next((model for model in MODELS if any(name.endswith('/'+model+'_'+view+'.glb')
                              for view in ('1P', '3P', 'UI'))), None)
                if match:
                    data = save(archive, name)
                    magic, version, length, size, kind = struct.unpack_from('<5I', data)
                    assert magic == 0x46546C67 and version == 2 and length == len(data) and kind == 0x4E4F534A
                    gltf = json.loads(data[20:20+size])
                    models[Path(name).stem] = name
                    for image in gltf.get('images', []):
                        uri = image.get('uri')
                        if uri and not uri.startswith('data:'):
                            needed.add(posixpath.normpath(posixpath.join(posixpath.dirname(name), uri)))
                if name.endswith('.psa') and ('/Guns/AR/Scar-H/' in name or '/Melees/Knife/Knife_FairbairnSykes_001/' in name):
                    save(archive, name)
                    if name not in animations: animations.append(name)
    found = set()
    for path in sorted(SOURCE.glob('*GenTexDir*.zip')):
        with zipfile.ZipFile(path) as archive:
            for name in needed.intersection(archive.namelist()):
                if name not in found:
                    save(archive, name)
                    found.add(name)
    assert needed == found, sorted(needed-found)
    with zipfile.ZipFile(next(SOURCE.glob('*DataTables*.zip'))) as archive:
        entry = next(n for n in archive.namelist() if n.endswith('/WeaponPartsMeshSocketNameInfo.json'))
        raw = archive.read(entry)
        rows = json.loads(raw)['Rows']
        sockets = {}
        for model in models:
            matches = [(k,v) for k,v in rows.items()
                       if k.startswith('/Game/Models/') and k.endswith('/'+model+'.'+model)]
            assert len(matches) == 1, model
            sockets[model] = {'path': matches[0][0], **matches[0][1]}
        (OUT/'AssemblySockets.json').write_text(json.dumps(sockets,indent=2)+'\n',encoding='utf-8')
        records.append({'archive': Path(archive.filename).name, 'entry': entry,
                        'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)})
    for model in MODELS:
        assert model+'_1P' in models and model+'_3P' in models, model
    manifest = {'schema': 1, 'models': models, 'animations': animations, 'files': records,
                'assembly_status': 'provisional_basic_parts_not_official_default',
                'knife_status': 'basic_straight_knife_candidate_default_unconfirmed',
                'runtime_validation': 'not_run'}
    (OUT/'Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'models': len(models), 'animations': len(animations), 'textures': len(found)}))


if __name__ == '__main__':
    main()
