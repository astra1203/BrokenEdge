"""Recover cooked climb actors, including template, socket and ISM transforms.

ZIPs are read-only. Mesh names select dependencies, never climb eligibility.
All matrices use column vectors in Unreal's centimetre coordinate system.
"""
import copy
import hashlib
import json
import math
from pathlib import Path
import sqlite3
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'Saved/SpaceCenter'
sys.path.insert(0, str(ROOT / 'Scripts/SpaceCenter'))
from source_packages import extract, package_key, read_names, write_json

CLASSES = ('BP_Interactor_ClimbLadder_TemplateNew_C', 'BP_Interactor_ClimbLadder_TemplateNew_Pipe_C')
TEMPLATES = ['/Game/BluePrints/Interact/ClimbLadder/' + name[:-2] for name in CLASSES]
LEGACY_IDS = ['29eeffe2c3f7d69c', '8a32fbd9fba8377c', 'ebd2e8d2078a954f']


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def canonical(path):
    return path.split('.')[0].replace('DeltaForce/Content/', '/Game/').replace('Engine/Content/', '/Engine/')


def identity(value):
    return hashlib.sha256(value.casefold().encode('utf-8')).hexdigest()[:16]


def vec(value, default=0):
    return [value.get(k, default) for k in 'XYZ']


def multiply(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def point(matrix, position=(0, 0, 0)):
    return [sum(matrix[i][j] * position[j] for j in range(3)) + matrix[i][3] for i in range(3)]


def matrix(properties, instance=False):
    if instance:
        x, y, z, w = [properties['Rotation'][k] for k in 'XYZW']
        norm = math.sqrt(x*x + y*y + z*z + w*w)
        if norm < .99 or norm > 1.01:
            raise ValueError('Invalid source quaternion')
        # CUE4Parse's fast matrix-to-quaternion normalisation is approximate.
        x, y, z, w = [v / norm for v in (x, y, z, w)]
    else:
        r = properties.get('RelativeRotation', {})
        p, y, r = [math.radians(r.get(k, 0)) / 2 for k in ('Pitch', 'Yaw', 'Roll')]
        sp, sy, sr, cp, cy, cr = math.sin(p), math.sin(y), math.sin(r), math.cos(p), math.cos(y), math.cos(r)
        x, y, z, w = cr*sp*sy-sr*cp*cy, -cr*sp*cy-sr*cp*sy, cr*cp*sy-sr*sp*cy, cr*cp*cy+sr*sp*sy
    rotation = [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]
    scale = vec(properties.get('Scale3D' if instance else 'RelativeScale3D', {}), 1)
    location = vec(properties.get('Translation' if instance else 'RelativeLocation', {}))
    return [[rotation[i][j]*scale[j] for j in range(3)] + [location[i]] for i in range(3)] + [[0, 0, 0, 1]]


def rebased(transform, origin):
    result = copy.deepcopy(transform)
    for i in range(3):
        result[i][3] -= origin[i]
    return result


class Exports:
    def __init__(self, packages):
        self.objects = {}
        for package in packages:
            if package.get('error') or package.get('failures'):
                raise ValueError('Unresolved exports: ' + package['package'])
            for obj in package['objects']:
                self.objects[package_key(package['package']) + '.' + str(obj['export_index'])] = obj

    def get(self, reference):
        path = reference['ObjectPath']
        package, index = path.rsplit('.', 1)
        return self.objects[package_key(package) + '.' + index]

    def properties(self, obj, seen=()):
        if id(obj) in seen:
            raise ValueError('Cyclic component template')
        result = {}
        template = obj.get('Template')
        if template and not template['ObjectPath'].startswith('/Script/'):
            result.update(self.properties(self.get(template), (*seen, id(obj))))
        result.update(obj.get('Properties', {}))
        return result

    def mesh(self, path):
        matches = [o for key, o in self.objects.items()
                   if key.rsplit('.', 1)[0] == package_key(canonical(path)) and o['Type'] == 'StaticMesh']
        if len(matches) != 1:
            raise ValueError('Mesh unresolved: ' + path)
        return matches[0]

    def transform(self, obj, seen=()):
        if id(obj) in seen:
            raise ValueError('Cyclic component attachment')
        props = self.properties(obj)
        local = matrix(props)
        parent = props.get('AttachParent')
        if not parent:
            return local
        parent = self.get(parent)
        socket = props.get('AttachSocketName')
        if socket and socket != 'None':
            mesh = self.mesh(self.properties(parent)['StaticMesh']['ObjectPath'])
            sockets = [self.get(ref) for ref in mesh['Properties']['Sockets']]
            matches = [s for s in sockets if s['Properties']['SocketName'] == socket]
            if len(matches) != 1:
                raise ValueError('Missing/ambiguous socket ' + socket)
            local = multiply(matrix(matches[0]['Properties']), local)
        return multiply(self.transform(parent, (*seen, id(obj))), local)


def convert(mode, packages, out):
    out.mkdir(parents=True, exist_ok=False)
    request = dict(mode=mode, source=str(BASE/'Source/DeltaForce'), output=str(out/'Parsed'), packages=sorted(set(packages)))
    write_json(out/'Request.json', request)
    command = [str(BASE/'Tools/dotnet/dotnet.exe'), str(ROOT/'Scripts/SpaceCenter/Converter/bin/Release/net10.0/SpaceCenter.Converter.dll'), str(out/'Request.json')]
    with (out/'Converter.log').open('wb') as log:
        run = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    write_json(out/'Process.json', dict(command=command, exit_code=run.returncode))
    if run.returncode:
        raise RuntimeError('Source converter failed: ' + str(out))
    return read(out/'Parsed/Packages.json')


def discover(out):
    """Scan all SpaceCenter gameplay headers, including HD companions."""
    with sqlite3.connect((BASE/'catalog.sqlite').resolve().as_uri()+'?mode=ro', uri=True) as db:
        db.row_factory = sqlite3.Row
        rows = db.execute("SELECT * FROM entries WHERE package >= ? AND package < ? AND ext IN ('.umap','.uasset') ORDER BY package",
                          ('deltaforce/content/maps/spacecenter_ma1/interactors/', 'deltaforce/content/maps/spacecenter_ma1/interactors0')).fetchall()
    if not rows or sum(row['size'] for row in rows) > 32*1024**2:
        raise ValueError('Unexpected source discovery size')
    selected, inventory, archives = [], [], {}
    try:
        for row in rows:
            archive = archives.setdefault(row['archive'], None)
            if archive is None:
                archive = archives[row['archive']] = zipfile.ZipFile(row['archive'])
            data = archive.read(row['path'])
            names = read_names(data)
            eligible = any(name in names for name in CLASSES)
            package = canonical(row['path'])
            inventory.append(dict(package=package, archive=Path(row['archive']).name, entry=row['path'],
                                  sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), class_reference=eligible))
            if eligible:
                selected.append(package)
    finally:
        for archive in archives.values():
            archive.close()
    write_json(out/'Discovery.json', inventory)
    if not selected:
        raise ValueError('No original climb interaction packages')
    return selected


def prepare(out):
    out.mkdir(parents=True, exist_ok=False)
    active = read(ROOT/'Scripts/SpaceCenter/restoration_active.json')
    profile = read(ROOT/active['profile_file'])
    if sha(ROOT/active['manifest_file']) != active['manifest_sha256']:
        raise ValueError('Active restoration manifest changed')
    packages = discover(out)
    dependencies = TEMPLATES + ['/Game/BluePrints/Interact/BP_Interactor_TopClimbLadder']
    extraction = [extract(packages + dependencies, limit_gib=.2)]
    if extraction[-1]['missing']:
        raise ValueError(extraction[-1]['missing'])
    parsed = convert('inspect', packages + dependencies, out/'Actors')
    objects = Exports(parsed)
    meshes = sorted({canonical(objects.properties(objects.get(actor['Properties'][name]))['StaticMesh']['ObjectPath'])
                     for p in parsed if p['package'] not in dependencies for actor in p['objects'] if actor['Type'] in CLASSES
                     for name in ('LadderMeshStart', 'InstancedStaticMesh', 'LadderMeshEnd')})
    extraction.append(extract(meshes, limit_gib=.2))
    if extraction[-1]['missing']:
        raise ValueError(extraction[-1]['missing'])
    mesh_parsed = convert('inspect', meshes, out/'MeshData')
    objects = Exports(parsed + mesh_parsed)
    ladders, excluded, unresolved = [], [], []
    for package in parsed:
        if package['package'] in dependencies:
            continue
        for actor in package['objects']:
            if actor['Type'] not in CLASSES:
                continue
            source = package['package'] + ':PersistentLevel.' + actor['Name']
            row = dict(id=identity(source), source_object=source, package=package['package'],
                       export_index=actor['export_index'], source_class=actor['class_reference'])
            try:
                if objects.get(actor['Outer'])['Type'] != 'Level':
                    raise ValueError('Interaction is not a saved level actor')
                props = objects.properties(actor)
                transform = objects.transform(objects.get(props['RootComponent']))
                location = point(transform)
                row.update(source_transform=transform, transform=rebased(transform, profile['origin']),
                           ladder_height=props['LadderHeight'], loop_count=props.get('LoopNum', 0))
                if not all(profile['minimum'][i] <= location[i] <= profile['maximum'][i] for i in range(2)):
                    excluded.append(row | dict(reason='outside_active_restoration_region'))
                    continue
                row['endpoints'] = {}
                row['triggers'] = {}
                for name in ('BottomBox', 'TopBox'):
                    component = objects.get(props[name])
                    t = objects.transform(component)
                    row['triggers'][name] = dict(source_reference=props[name], transform=rebased(t, profile['origin']),
                                                extent=vec(objects.properties(component)['BoxExtent']))
                for name in ('LadderBottomStartTransform', 'LadderBottomEndTransform', 'LadderTopStartTransform', 'LadderTopEndTransform'):
                    component = objects.get(props[name])
                    t = objects.transform(component)
                    row['endpoints'][name] = dict(source_reference=props[name], source_transform=t, transform=rebased(t, profile['origin']))
                row['components'] = []
                for name in ('LadderMeshStart', 'InstancedStaticMesh', 'LadderMeshEnd'):
                    component = objects.get(props[name])
                    path = canonical(objects.properties(component)['StaticMesh']['ObjectPath'])
                    component_transform = objects.transform(component)
                    instances = component.get('PerInstanceSMData') if name == 'InstancedStaticMesh' else [None]
                    if instances is None or (name == 'InstancedStaticMesh' and len(instances) != row['loop_count']):
                        raise ValueError('Cooked middle segment count missing/differs from LoopNum')
                    for index, instance in enumerate(instances):
                        t = multiply(component_transform, matrix(instance['TransformData'], True)) if instance else component_transform
                        row['components'].append(dict(name=name, instance=index, source_reference=props[name], mesh=path,
                                                      source_transform=t, transform=rebased(t, profile['origin'])))
                ladders.append(row)
            except (KeyError, ValueError) as error:
                unresolved.append(row | dict(reason=str(error)))
    geometry = convert('export', meshes, out/'Geometry')
    mesh_entries = {}
    for package in geometry:
        candidates = list((out/'Geometry/Parsed').rglob(package['package'].rsplit('/', 1)[-1]+'*.glb'))
        if len(candidates) != 1:
            raise ValueError('Expected one original GLB: ' + package['package'])
        path = package['package']
        mesh_entries[path] = dict(source=path, file=str(candidates[0]), sha256=sha(candidates[0]),
                                  source_mesh=objects.mesh(path),
                                  sockets=[objects.get(r) for r in objects.mesh(path)['Properties'].get('Sockets', [])])
    materials = sorted({canonical(slot['MaterialInterface']['ObjectPath']) for entry in mesh_entries.values()
                        for slot in entry['source_mesh']['Properties']['StaticMaterials']})
    extraction.append(extract(materials, limit_gib=.1))
    material_data = convert('inspect', materials, out/'Materials')
    material_entries, textures = {}, set()
    for package in material_data:
        props = package['objects'][0]['Properties']
        # This recipe matches the existing SpaceCenter NRMAC reconstruction.
        if canonical(props['Parent']['ObjectPath']).rsplit('/', 1)[-1] != 'M_SW_NRMAC_Master':
            raise ValueError('Unsupported original ladder material parent')
        values = {v['ParameterInfo']['Name']: canonical(v['ParameterValue']['ObjectPath']) for v in props['TextureParameterValues']}
        if set(values) != {'BaseColorMap', 'NRMap'}:
            raise ValueError('Unexpected ladder texture recipe')
        textures.update(values.values())
        material_entries[package['package']] = dict(source=package['package'], properties=props, textures=values, recipe='NRMAC')
    extraction.append(extract(sorted(textures), limit_gib=.1))
    texture_data = convert('texture-mips', textures, out/'Textures')
    texture_entries = {}
    for package in texture_data:
        obj = package['objects'][0]
        decoded = next((attempt for attempt in obj['mip_attempts'] if attempt.get('file')), None)
        if decoded is None:
            raise ValueError('Original ladder texture failed to decode')
        texture_entries[package['package']] = decoded | dict(sha256=sha(decoded['file']), source=package['package'], srgb=obj['srgb'])
    if any(entry['missing'] for entry in extraction):
        raise ValueError('Missing original ladder dependency')
    write_json(out/'Extractions.json', extraction)
    report = dict(schema=2, baseline='current_unpacked_SpaceCenter_Ma1_interaction_actors', origin=profile['origin'],
                  minimum=profile['minimum'], maximum=profile['maximum'], manifest_sha256=active['manifest_sha256'],
                  discovery=str(out/'Discovery.json'), extractions=str(out/'Extractions.json'), ladders=ladders,
                  excluded=excluded, unresolved=unresolved, meshes=mesh_entries, materials=material_entries,
                  textures=texture_entries, legacy_binding_ids=LEGACY_IDS,
                  boundary='Source reconstruction only; collision, rendering, traversal and network acceptance are separate.')
    write_json(out/'Manifest.json', report)
    if unresolved:
        raise RuntimeError('Unresolved source ladders recorded: ' + str(out/'Manifest.json'))
    if not ladders or len({r['id'] for r in ladders}) != len(ladders):
        raise ValueError('Empty or duplicate source ladder inventory')
    return report
