"""Discover original scene layers independently of the historical sample selectors."""
import argparse
import collections
import copy
import json
import math
import sqlite3
from restoration_common import BASE, WORK, ROOT, MINIMUM, MAXIMUM, ORIGIN, read, write_json, canonical, identity, extract_recorded, run_converter

GEOMETRY_GROUPS = {'BigMeshGroup', 'MidMeshGroup', 'SmallMeshGroup', 'Room', 'Landmark',
                   'Decorations', 'Decorations_HQ', 'Decorations_SQ', 'Decorations_SSQ',
                   'MidDecorations', 'Default', 'POIResident'}
FOLIAGE_GROUPS = {'Tree', 'Bushes', 'Grass', 'DecorationFoliage'}


def discover(tag):
    out = WORK / 'DiscoveryRuns' / tag
    out.mkdir(parents=True, exist_ok=False)
    config = read(ROOT / 'Scripts/SpaceCenter/north_config.json')
    from pathlib import Path
    for archive in read(BASE / 'ArchiveIndex.json')['archives']:
        current = (Path(config['source']) / archive['archive']).stat()
        if current.st_size != archive['size'] or current.st_mtime_ns != archive['mtime_ns']:
            raise RuntimeError('Original source archive changed: ' + archive['archive'])
    database = sqlite3.connect((BASE / 'catalog.sqlite').resolve().as_uri() + '?mode=ro', uri=True)
    rows = database.execute("SELECT DISTINCT path FROM entries WHERE package LIKE "
                            "'deltaforce/content/maps/buildmaps_hd/spacecenter_ma1/runtime/%' "
                            "AND ext IN ('.uasset','.umap')").fetchall()
    database.close()
    selected, inventory = set(), []
    for (path,) in rows:
        group = path.split('/Runtime/')[1].split('/')[0]
        reason = 'candidate_static_layout' if group in GEOMETRY_GROUPS else 'separate_foliage_format' if group in FOLIAGE_GROUPS else 'non_render_layout_or_duplicate_proxy'
        inventory.append({'package': canonical(path), 'group': group, 'selection': reason})
        if group in GEOMETRY_GROUPS and '_LOD' not in path:
            selected.add(canonical(path))
    write_json(out / 'PackageInventory.json', inventory)
    packages, layouts = sorted(selected), []
    for start in range(0, len(packages), 60):
        batch = out / ('batch-%03d' % start)
        extract_recorded(packages[start:start + 60], batch)
        layouts += run_converter('layout', packages[start:start + 60], batch / 'Layout', partial=True)
        print('LAYOUT', start, '/', len(packages), flush=True)
    write_json(out / 'Layouts.json', layouts)
    errors = [{'package': p['package'], 'error': p.get('error'), 'failures': p.get('failures')}
              for p in layouts if p.get('error') or p.get('failures')]
    report = {'result': 'discovered_with_failures' if errors else 'discovered', 'packages': len(packages),
              'components': sum(len(p.get('objects', [])) for p in layouts), 'errors': errors,
              'origin': ORIGIN, 'minimum': MINIMUM, 'maximum': MAXIMUM,
              'groups': dict(collections.Counter(row['group'] for row in inventory)),
              'boundary': 'Discovery only; bounds selection, geometry and native rendering remain unvalidated.'}
    write_json(out / 'Report.json', report)
    print({k: v for k, v in report.items() if k not in {'errors', 'groups'}})


def bounds(tag, discovery):
    out = WORK / 'BoundsRuns' / tag
    out.mkdir(parents=True, exist_ok=False)
    layouts = read(WORK / 'DiscoveryRuns' / discovery / 'Layouts.json')
    meshes = sorted({canonical(o['mesh']['ObjectPath']) for p in layouts for o in p.get('objects', [])
                     if isinstance(o.get('mesh'), dict) and o['mesh'].get('ObjectPath')})
    cache = {}
    for folder in [BASE / 'Converted01', BASE / 'Recovered01', BASE / 'Phase7A/Converted01', BASE / 'Phase7A/Recovered01']:
        for p in read(folder / 'Packages.json'):
            for o in p.get('objects', []):
                if o.get('class') == 'StaticMesh' and o.get('bounds'):
                    cache[canonical(o['object_path'])] = o
    pending = [p for p in meshes if p not in cache]
    failures = []
    for start in range(0, len(pending), 40):
        batch = out / ('batch-%04d' % start)
        extract_recorded(pending[start:start+40], batch)
        parsed = run_converter('metadata', pending[start:start+40], batch / 'Metadata', partial=True)
        for package in parsed:
            valid = [o for o in package.get('objects', []) if o.get('class') == 'StaticMesh' and o.get('bounds')]
            if not valid:
                failures.append(package)
            for obj in valid:
                cache[canonical(obj['object_path'])] = obj
        write_json(out / 'MeshMetadata.json', cache)
        write_json(out / 'Failures.json', failures)
        print('BOUNDS',start,'/',len(pending),'VALID',len(cache),'FAILED',len(failures),flush=True)
    write_json(out / 'Report.json', {'requested':len(meshes),'resolved':len(cache),'failures':len(failures)})


def world_bounds(actor, metadata):
    transform = actor['transform']
    q,t,s = [transform[k] for k in ['Rotation','Translation','Scale3D']]
    x,y,z,w = [q[k] for k in 'XYZW']
    matrix = [[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
              [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
              [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
    center = [t[k]+sum(matrix[i][j]*s[a]*metadata['bounds']['Origin'][a] for j,a in enumerate('XYZ')) for i,k in enumerate('XYZ')]
    extent = [sum(abs(matrix[i][j]*s[a])*metadata['bounds']['BoxExtent'][a] for j,a in enumerate('XYZ')) for i in range(3)]
    assert all(math.isfinite(v) for v in center+extent)
    return {'min':[c-e for c,e in zip(center,extent)], 'max':[c+e for c,e in zip(center,extent)]}


def select(tag, discovery, bounds_tag):
    out=WORK/'SelectionRuns'/tag
    out.mkdir(parents=True,exist_ok=False)
    metadata=read(WORK/'BoundsRuns'/bounds_tag/'MeshMetadata.json')
    failed=read(WORK/'BoundsRuns'/bounds_tag/'Failures.json')
    # The existing parser only exposes a geometry snapshot after a proven tail failure.
    # Recovery stays scoped to this explicit source list; it never fabricates geometry.
    for start in range(0,len(failed),20):
        batch=[canonical(p['package']) for p in failed[start:start+20]]
        recovered=run_converter('metadata',batch,out/('Recovery-%03d'%start),recovery=batch,partial=True)
        for p in recovered:
            for o in p.get('objects',[]):
                if o.get('class')=='StaticMesh' and o.get('bounds'):
                    metadata[canonical(o['object_path'])]=o
    layouts=read(WORK/'DiscoveryRuns'/discovery/'Layouts.json')
    known={a['source_object']:a['id'] for directory in [BASE,BASE/'Phase7A'] for a in read(directory/'ImportManifest.json')['actors']}
    candidates,excluded,unresolved=[],[],[]
    for package in layouts:
        for obj in package.get('objects',[]):
            mesh=canonical(obj['mesh']['ObjectPath']) if isinstance(obj.get('mesh'),dict) and obj['mesh'].get('ObjectPath') else None
            reason=None
            if not mesh:
                reason='no_static_mesh_reference'
            elif mesh not in metadata:
                unresolved.append({'source':obj['source_object'],'mesh':mesh,'transform':obj['transform']})
                continue
            else:
                measured=world_bounds(obj,metadata[mesh])
                if any(measured['max'][i]<MINIMUM[i] or measured['min'][i]>MAXIMUM[i] for i in [0,1]):
                    reason='bounds_outside_region'
                elif obj.get('properties',{}).get('bVisible',True) is False:
                    reason='source_hidden'
                elif '/FogMesh/' in mesh or '/Fog_HD/' in mesh:
                    reason='fog_volume_requires_environment_reconstruction'
                elif obj.get('instances'):
                    raise RuntimeError('Instance transforms require measured expansion: '+obj['source_object'])
            if reason:
                excluded.append({'source':obj['source_object'],'mesh':mesh,'reason':reason})
                continue
            actor=copy.deepcopy(obj)
            actor.update(id=known.get(obj['source_object'],identity(obj['source_object'])),mesh=mesh,
                         source_package=package['package'],bounds=measured,existing=obj['source_object'] in known)
            slots=[canonical(m['MaterialInterface']['ObjectPath']) if m.get('MaterialInterface') else None for m in metadata[mesh].get('materials',[])]
            for i,ref in enumerate(actor.get('properties',{}).get('OverrideMaterials',[]) or []):
                while len(slots)<=i:slots.append(None)
                if ref:slots[i]=canonical(ref['ObjectPath'])
            actor['materials']=slots
            candidates.append(actor)
    actors,seen=[],{}
    # Preserve existing actor identities when identical records occur in multiple layers.
    for a in sorted(candidates,key=lambda a:(not a['existing'],a['source_object'])):
        key=json.dumps([a['mesh'],a['transform'],a['materials']],sort_keys=True)
        if key in seen:
            excluded.append({'source':a['source_object'],'mesh':a['mesh'],'reason':'identical_placement','retained':seen[key]})
        else:
            seen[key]=a['id'];actors.append(a)
    meshes={a['mesh'] for a in actors}
    write_json(out/'MeshMetadata.json',{m:metadata[m] for m in sorted(meshes)})
    write_json(out/'Selection.json',{'version':2,'origin':ORIGIN,'minimum':MINIMUM,'maximum':MAXIMUM,
                                   'actors':actors,'excluded':excluded,'unresolved':unresolved})
    report={'result':'selected' if not unresolved else 'selected_with_unresolved_geometry','actors':len(actors),
            'meshes':len(meshes),'materials':len({m for a in actors for m in a['materials'] if m}),
            'retained_existing':sum(a['existing'] for a in actors),'unresolved':len(unresolved),
            'excluded':dict(collections.Counter(a['reason'] for a in excluded)),
            'quality_layers':'Distinct decorative placements retained; exact duplicates removed, no assumed quality-name hierarchy.'}
    write_json(out/'Report.json',report)
    print(report,flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('task', choices=['discover', 'bounds', 'select'])
    parser.add_argument('--tag', required=True)
    parser.add_argument('--discovery', default='Phase7CDiscover01')
    parser.add_argument('--bounds', default='Phase7CBounds01')
    args = parser.parse_args()
    if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
        parser.error('Use a new alphanumeric tag')
    if args.task == 'discover':
        discover(args.tag)
    elif args.task == 'bounds':
        bounds(args.tag, args.discovery)
    else:
        select(args.tag,args.discovery,args.bounds)
