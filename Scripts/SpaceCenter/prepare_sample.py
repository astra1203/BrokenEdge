"""Produce the explicit import manifest, retaining every material approximation."""
import hashlib
import json
from pathlib import Path
from source_packages import WORK, PROJECT, write_json


def canonical(path):
    path = path.split('.')[0]
    return path.replace('DeltaForce/Content/', '/Game/')


def identifier(path):
    return hashlib.sha256(path.encode()).hexdigest()[:16]


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def prepare():
    selection = read(WORK / 'Selection01.json')
    mesh_meta, meshes, textures, materials = {}, {}, {}, {}
    for folder in ['Converted01', 'Recovered01']:
        for package in read(WORK / folder / 'Packages.json'):
            for obj in package.get('objects', []):
                if obj['class'] == 'StaticMesh':
                    mesh_meta[canonical(obj['object_path'])] = obj
        for result in read(WORK / folder / 'ExportResults.json'):
            if result['Success']:
                path = canonical(result['ObjectPath'])
                meshes[path] = {'id': identifier(path), 'file': result['DiskFilePaths'][0],
                                'source': path, 'metadata': mesh_meta[path]}
    texture_results = read(WORK / 'Textures01/ExportResults.json')
    for result in texture_results:
        if result['Success']:
            path = canonical(result['ObjectPath'])
            textures[path] = {'id': identifier(path), 'file': result['DiskFilePaths'][0], 'source': path}
    for package in read(WORK / 'Materials01/Packages.json'):
        for obj in package.get('objects', []):
            if 'Material' not in obj['Type'] or 'Expression' in obj['Type']:
                continue
            path = canonical(package['package'])
            props = obj.get('Properties', {})
            tex = {p['ParameterInfo']['Name']: canonical(p['ParameterValue']['ObjectPath'])
                   for p in props.get('TextureParameterValues', []) if p.get('ParameterValue')}
            vectors = {p['ParameterInfo']['Name']: p['ParameterValue'] for p in props.get('VectorParameterValues', [])}
            scalars = {p['ParameterInfo']['Name']: p['ParameterValue'] for p in props.get('ScalarParameterValues', [])}
            choices = {}
            for channel, names in {
                'base': ['BaseColorMap', 'BaseColor', 'BAMap', '01_R_Base', '01_Base', 'BaseTex', 'ColorMap', 'Diffuse'],
                'normal': ['NormalMap', '01_R_Normal', '01_Normal', 'Normal'],
                'emissive': ['EmissiveMap', 'EMMap']}.items():
                choice = next(((name, tex[name]) for name in names if name in tex and tex[name] in textures), None)
                choices[channel] = {'parameter': choice[0], 'texture': choice[1]} if choice else None
            chosen_base = choices['base']['parameter'] if choices['base'] else None
            tint = vectors.get('BaseColorScaler_01_R' if chosen_base == '01_R_Base' else 'BaseColorScaler', {})
            color = [tint.get(c, 1) for c in ['R', 'G', 'B']]
            materials[path] = {'id': identifier(path), 'source': path, 'source_type': obj['Type'],
                               'channels': choices, 'tint': color,
                               'uv_scale': scalars.get('Texcoord_01_R', 1) if chosen_base == '01_R_Base' else 1,
                               'roughness': scalars.get('Layer01Roughness', .65),
                               'metallic': scalars.get('Layer01Metal', 0),
                               'glass': 'glass' in path.lower(),
                               'source_texture_parameters': tex,
                               'unavailable_texture_parameters': {k: v for k, v in tex.items() if v not in textures},
                               'unconverted_parameters': sorted(k for k in tex if k not in [v['parameter'] for v in choices.values() if v]),
                               'method': 'Rebuilt PBR from available named texture parameters; original shader graph and layered/weather effects not restored'}
    actors, excluded, seen = [], [], {}
    for actor in selection['actors']:
        path = canonical(actor['mesh']['ObjectPath'])
        reason = None
        if path not in meshes:
            raise RuntimeError('Selected geometry was not converted: ' + path)
        if actor.get('instances'):
            raise RuntimeError('Instanced placements require explicit expansion before import: ' + actor['source_object'])
        elif '/FogMesh/' in path or '/Fog_HD/' in path:
            reason = 'specialized_fog_volume_not_opaque_geometry'
        if reason:
            excluded.append({'source': actor['source_object'], 'mesh': path, 'reason': reason})
            continue
        key = json.dumps([path, actor['transform'], actor.get('properties', {}).get('OverrideMaterials')], sort_keys=True)
        if key in seen:
            excluded.append({'source': actor['source_object'], 'reason': 'identical_placement', 'retained': seen[key]})
            continue
        identity = identifier(actor['source_object'])
        seen[key] = identity
        actor['id'] = identity
        actor['mesh'] = path
        actors.append(actor)
    used_materials = set()
    for actor in actors:
        meta = meshes[actor['mesh']]['metadata']
        slots = [canonical(m['MaterialInterface']['ObjectPath']) if m.get('MaterialInterface') else None for m in meta.get('materials', [])]
        for i, ref in enumerate(actor.get('properties', {}).get('OverrideMaterials', []) or []):
            while len(slots) <= i:
                slots.append(None)
            if ref:
                slots[i] = canonical(ref['ObjectPath'])
        actor['materials'] = slots
        used_materials.update(s for s in slots if s)
    used_textures = set(v['texture'] for p, m in materials.items() if p in used_materials for v in m['channels'].values() if v)
    manifest = {'version': 1, 'origin': selection['origin'], 'actors': actors,
                'meshes': meshes, 'materials': {p: m for p, m in materials.items() if p in used_materials},
                'textures': {p: t for p, t in textures.items() if p in used_textures},
                'excluded': excluded, 'missing_materials': sorted(used_materials - set(materials))}
    if manifest['missing_materials']:
        raise RuntimeError('Selected material slots were not resolved: ' + str(manifest['missing_materials']))
    write_json(WORK / 'ImportManifest.json', manifest)
    print(json.dumps({k: len(manifest[k]) for k in ['actors', 'meshes', 'materials', 'textures', 'excluded', 'missing_materials']}))


if __name__ == '__main__':
    prepare()
