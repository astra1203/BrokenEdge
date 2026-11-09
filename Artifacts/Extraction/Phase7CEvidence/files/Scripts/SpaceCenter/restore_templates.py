"""Resolve cooked component defaults from the actor's exact Blueprint class."""
from restoration_common import *
import argparse
from restore_scene import select


def prepare(tag):
    out=WORK/'TemplateRuns'/tag
    maps=read(WORK/'ProbeRuns/Phase7CProbe02/Inspect/Parsed/Packages.json')
    blueprints={p['package']:p for p in read(WORK/'ProbeRuns/Phase7CProbe05/Inspect/Parsed/Packages.json')}
    mappings={};unresolved=[]
    for p in maps:
        if '/Default/' not in p['package']:continue
        objects={o['export_index']:o for o in p['objects']}
        local={}
        for index,o in objects.items():
            if 'Component' not in o['Type'] or not o.get('Outer'):continue
            reference=o['Outer']['ObjectPath']
            if canonical(reference)!=p['package']:continue
            owner=objects.get(int(reference.rsplit('.',1)[1]),{})
            ref=owner.get('class_reference')
            if not ref or not ref['ObjectPath'].startswith('/Game/'):continue
            class_package=canonical(ref['ObjectPath'])
            candidates=[c for c in blueprints.get(class_package,{}).get('objects',[])
                        if c['Type']==o['Type'] and c['Name'] in [o['Name'],o['Name']+'_GEN_VARIABLE']]
            if len(candidates)>1:raise RuntimeError('Ambiguous source component template')
            if candidates:local[str(index)]={'package':class_package,'export_index':candidates[0]['export_index']}
            elif o['Type']=='StaticMeshComponent' and not o.get('Properties',{}).get('StaticMesh'):
                unresolved.append({'package':p['package'],'index':index,'name':o['Name'],'blueprint':class_package})
        mappings[p['package']]=local
    write_json(out/'ComponentTemplates.json',mappings)
    parsed=run_converter('layout',list(mappings),out/'Layout',extra={'component_templates':mappings},partial=True)
    old=read(WORK/'DiscoveryRuns/Phase7CDiscover01/Layouts.json')
    replacement={p['package']:p for p in parsed}
    discovery='Phase7CDiscover02'
    write_json(WORK/'DiscoveryRuns'/discovery/'Layouts.json',[replacement.get(p['package'],p) for p in old])
    metadata=read(WORK/'BoundsRuns/Phase7CBounds01/MeshMetadata.json')
    metadata.update(read(WORK/'SelectionRuns/Phase7CSelect03/MeshMetadata.json'))
    missing=sorted({canonical(o['mesh']['ObjectPath']) for p in parsed for o in p.get('objects',[])
                    if (o.get('mesh') or {}).get('ObjectPath')}-set(metadata))
    failures=read(WORK/'BoundsRuns/Phase7CBounds01/Failures.json')
    for start in range(0,len(missing),20):
        batch=missing[start:start+20];folder=out/('Meshes-%03d'%start)
        extract_recorded(batch,folder)
        records=run_converter('metadata',batch,folder/'Metadata',partial=True)
        for p in records:
            valid=[o for o in p.get('objects',[]) if o.get('bounds')]
            if not valid:failures.append(p)
            for o in valid:metadata[canonical(o['object_path'])]=o
    bounds='Phase7CBounds02'
    write_json(WORK/'BoundsRuns'/bounds/'MeshMetadata.json',metadata)
    write_json(WORK/'BoundsRuns'/bounds/'Failures.json',failures)
    write_json(out/'Report.json',{'mapped_components':sum(map(len,mappings.values())),
                                'unresolved_templates':unresolved,'new_meshes':missing,
                                'layout_failures':[p['package'] for p in parsed if p.get('error') or p.get('failures')]})
    select('Phase7CSelect04',discovery,bounds)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--tag',required=True);args=parser.parse_args();prepare(args.tag)
