"""Combine bounded source foliage with the verified building selection."""
import argparse
import copy
import json
from restoration_common import *
from restore_scene import world_bounds


def main(tag):
    out=WORK/'GeometryRuns'/tag
    scene=read(input_path('static_selection','SelectionRuns/Phase7CSelect04/Selection.json'))
    metadata=read(input_path('static_metadata','SelectionRuns/Phase7CSelect04/MeshMetadata.json'))
    foliage=read(input_path('foliage','FoliageRuns/Phase7CFoliage05/Decoded.json'))
    packages={p['package']:p for p in read(input_path('foliage_probe','ProbeRuns/Phase7CProbe06/Inspect/Parsed/Packages.json'))}
    missing=sorted({a['mesh'] for a in foliage}-set(metadata));failures=[]
    for start in range(0,len(missing),24):
        batch=missing[start:start+24];folder=out/('Meshes-%03d'%start)
        extract_recorded(batch,folder)
        records=run_converter('metadata',batch,folder/'Metadata',partial=True)
        retry=[p['package'] for p in records if not any(o.get('bounds') for o in p.get('objects',[]))]
        if retry:records+=run_converter('metadata',retry,folder/'Recovery',recovery=retry,partial=True)
        for p in records:
            for obj in p.get('objects',[]):
                if obj.get('bounds'):metadata[canonical(obj['object_path'])]=obj
        failures+=sorted(set(batch)-set(metadata))
    if failures:raise RuntimeError('Missing foliage mesh bounds: '+str(failures))
    actors=list(scene['actors']);excluded=[]
    keys={json.dumps([a['mesh'],a['transform'],a['materials']],sort_keys=True) for a in actors}
    for a in foliage:
        bounds=world_bounds(a,metadata[a['mesh']])
        if any(bounds['max'][i]<MINIMUM[i] or bounds['min'][i]>MAXIMUM[i] for i in [0,1]):
            excluded.append({'source':a['source_object'],'reason':'foliage_bounds_outside_region'});continue
        a=copy.deepcopy(a);a['bounds']=bounds
        a['materials']=[canonical(m['MaterialInterface']['ObjectPath']) if m.get('MaterialInterface') else None for m in metadata[a['mesh']].get('materials',[])]
        for index,ref in enumerate(a.pop('material_indices')):
            while len(a['materials'])<=index:a['materials'].append(None)
            if ref<0:
                source_ref=packages[a['source_package']]['imports'][-ref-1]
                a['materials'][index]=canonical(source_ref['ObjectPath'])
            elif ref>0:raise ValueError('Unexpected foliage material export reference')
        key=json.dumps([a['mesh'],a['transform'],a['materials']],sort_keys=True)
        if key in keys:excluded.append({'source':a['source_object'],'reason':'identical_placement'});continue
        actors.append(a);keys.add(key)
    for a in actors:
        # Only non-colliding decoration within the existing North stream boundary moves out.
        # All walkable/supporting geometry remains resident.
        p=a['transform']['Translation'];body=a.get('properties',{}).get('BodyInstance',{})
        no_collision=body.get('CollisionProfileName')=='NoCollision' or body.get('CollisionEnabled')=='ECollisionEnabled::NoCollision'
        a['region']='North' if no_collision and -445000<=p['Y']<=-439000 and 659000<=p['X']<=671000 else 'Persistent'
    scene['actors']=sorted(actors,key=lambda a:a['id']);scene['excluded']+=excluded
    selected={a['mesh'] for a in actors}
    target=input_path('selection','SelectionRuns/Phase7CSelect05/Selection.json').parent
    write_json(target/'Selection.json',scene);write_json(target/'MeshMetadata.json',{p:metadata[p] for p in sorted(selected)})
    write_json(out/'Report.json',{'actors':len(actors),'foliage':sum(a.get('foliage',False) for a in actors),
                                'meshes':len(selected),'materials':len({m for a in actors for m in a['materials'] if m}),
                                'excluded_foliage':len(excluded)})
    print(read(out/'Report.json'),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--tag',required=True);main(p.parse_args().tag)
