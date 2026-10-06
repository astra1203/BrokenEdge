"""Export only selected scene dependencies, preserving errors and inherited parameters."""
import argparse
import gzip
import json
import sqlite3
from restoration_common import ROOT, BASE, WORK, read, sha, identity, canonical, write_json, extract_recorded, run_converter


def references(value):
    if isinstance(value,dict):
        if isinstance(value.get('ObjectPath'),str):
            yield value
        for child in value.values():yield from references(child)
    elif isinstance(value,list):
        for child in value:yield from references(child)


def existing_exports(kind):
    result={}
    folders=([BASE/'Converted01',BASE/'Recovered01',BASE/'Phase7A/Converted01',BASE/'Phase7A/Recovered01']
             if kind=='mesh' else [BASE/'Textures01',BASE/'AdditionalTextures01',BASE/'Phase7A/Textures01'])
    for folder in folders:
        target=folder/'ExportResults.json'
        if not target.exists():continue
        for row in read(target):
            if row['Success']:
                path=canonical(row['ObjectPath'])
                result[path]={'id':identity(path),'source':path,'file':row['DiskFilePaths'][0],
                              'files':row['DiskFilePaths'],'export_report':str(target)}
    pattern='AssetRuns/*/Meshes.json' if kind=='mesh' else 'TextureRuns/*/Textures.json'
    for target in sorted(WORK.glob(pattern)):result.update(read(target))
    recovery=WORK/'TextureRecoveryRuns/Phase7CRecoverTextures03/Textures.json'
    if kind=='texture' and recovery.exists():result.update(read(recovery))
    return result


def export_meshes(tag, selection):
    out=WORK/'AssetRuns'/tag
    out.mkdir(parents=True,exist_ok=False)
    metadata=read(WORK/'SelectionRuns'/selection/'MeshMetadata.json')
    cache=existing_exports('mesh')
    pending=sorted(set(metadata)-set(cache)); failures=[]
    for start in range(0,len(pending),24):
        batch=pending[start:start+24]
        recovery=[p for p in batch if metadata[p].get('recovery')]
        parsed=run_converter('export',batch,out/('Meshes-%04d'%start),recovery=recovery,partial=True)
        results_path=out/('Meshes-%04d'%start)/'Parsed/ExportResults.json'
        for row in read(results_path):
            if row['Success']:
                path=canonical(row['ObjectPath'])
                cache[path]={'id':identity(path),'source':path,'file':row['DiskFilePaths'][0],
                             'files':row['DiskFilePaths'],'export_report':str(results_path)}
            else:failures.append(row)
        print('MESHES',start,'/',len(pending),'FAILED',len(failures),flush=True)
    missing=sorted(set(metadata)-set(cache))
    write_json(out/'Meshes.json',{p:cache[p]|{'metadata':metadata[p]} for p in sorted(metadata) if p in cache})
    write_json(out/'Report.json',{'result':'exported' if not missing else 'failed','meshes':len(metadata),
                                 'missing':missing,'failures':failures})
    if missing:raise RuntimeError('Selected meshes failed export; see '+str(out))


def material_graph(tag, selection):
    out=WORK/'MaterialRuns'/tag
    out.mkdir(parents=True,exist_ok=False)
    scene=read(WORK/'SelectionRuns'/selection/'Selection.json')
    graph=json.loads(gzip.decompress((ROOT/'Docs/Extraction/Phase7BEvidence/materials/Graph.json.gz').read_bytes()))
    for target in sorted(WORK.glob('MaterialRuns/*/Graph.json')):graph.update(read(target))
    roots={m for a in scene['actors'] for m in a['materials'] if m and m.startswith('/Game/')}
    pending=roots-set(graph); missing=set(); failed=[]; index=0
    db=sqlite3.connect((BASE/'catalog.sqlite').resolve().as_uri()+'?mode=ro',uri=True)
    from source_packages import package_key
    while pending:
        packages=sorted(pending);pending=set()
        for start in range(0,len(packages),50):
            batch=[]
            for path in packages[start:start+50]:
                if db.execute('SELECT 1 FROM entries WHERE package=? LIMIT 1',(package_key(path),)).fetchone():batch.append(path)
                else:missing.add(path)
            if not batch:continue
            folder=out/('Batch-%03d'%index);index+=1
            extract_recorded(batch,folder)
            parsed=run_converter('inspect',batch,folder/'Inspect',partial=True)
            for package in parsed:
                name=canonical(package['package']);graph[name]=package
                if package.get('error') or package.get('failures'):failed.append(package)
                for ref in references(package.get('objects',[])):
                    target=canonical(ref['ObjectPath']);kind=ref.get('ObjectName','').split("'",1)[0].split(':')[-1]
                    if kind in {'Material','MaterialFunction','MaterialFunctionInstance','MaterialInstanceConstant',
                                'VirtualMaterialInstanceConstant','MaterialParameterCollection'} and target.startswith('/Game/') and target not in graph and target not in missing:
                        pending.add(target)
            print('MATERIALS',len(graph),'PENDING',len(pending),flush=True)
        pending.difference_update(graph)
    db.close()
    write_json(out/'Graph.json',dict(sorted(graph.items())))
    write_json(out/'Report.json',{'roots':sorted(roots),'packages':len(graph),'missing':sorted(missing),
                                 'parse_failures':failed,'boundary':'Inherited parameters and retained nodes; not recovery of stripped shader graphs.'})


def flatten(graph,path,stack=()):
    if path in stack:raise ValueError('Cyclic material inheritance: '+path)
    package=graph.get(path)
    result={'chain':[],'textures':{},'scalars':{},'vectors':{},'switches':{},'parameter_records':[], 'base_properties':{}}
    if not package:return result|{'missing_parent':path}
    obj=next((o for o in package.get('objects',[]) if o.get('Type') in {'Material','MaterialInstanceConstant','VirtualMaterialInstanceConstant'}),None)
    if not obj:return result|{'missing_material_object':path}
    props=obj.get('Properties',{})
    if props.get('Parent'):
        result=flatten(graph,canonical(props['Parent']['ObjectPath']),stack+(path,))
    result['chain'].append(path)
    # Only defaults actually present in the source are claimed. Missing defaults stay explicit.
    for expression in package.get('objects',[]):
        p=expression.get('Properties',{});name=p.get('ParameterName');kind=expression.get('Type','')
        if not name:continue
        if kind in {'MaterialExpressionTextureSampleParameter2D','MaterialExpressionTextureObjectParameter'} and p.get('Texture'):
            result['textures'].setdefault(name,canonical(p['Texture']['ObjectPath']))
        elif kind=='MaterialExpressionScalarParameter':result['scalars'].setdefault(name,p.get('DefaultValue',0.))
        elif kind=='MaterialExpressionVectorParameter':result['vectors'].setdefault(name,p.get('DefaultValue',{'R':0,'G':0,'B':0,'A':0}))
        elif kind in {'MaterialExpressionStaticSwitchParameter','MaterialExpressionStaticBoolParameter'}:result['switches'].setdefault(name,p.get('DefaultValue',False))
    for field,key in [('TextureParameterValues','textures'),('ScalarParameterValues','scalars'),('VectorParameterValues','vectors')]:
        for p in props.get(field,[]):
            info=p['ParameterInfo'];value=p.get('ParameterValue')
            result['parameter_records'].append({'source':path,'kind':key,'info':info,'value':value})
            # Preserve layer-associated parameters without conflating them with globals.
            if info.get('Association','GlobalParameter')!='GlobalParameter':continue
            result[key][info['Name']]=canonical(value['ObjectPath']) if key=='textures' and value else value
    for p in (props.get('StaticParameters') or {}).get('StaticSwitchParameters',[]):
        if p.get('bOverride',True):result['switches'][p['ParameterInfo']['Name']]=p['Value']
    for key in ['BlendMode','TwoSided','OpacityMaskClipValue','ShadingModel']:
        if key in props:result['base_properties'][key]=props[key]
    overrides=props.get('BasePropertyOverrides',{})
    # Cooked structs also serialize default values for fields that are not overridden.
    for key,value in overrides.items():
        if key.startswith('bOverride_'):continue
        if overrides.get('bOverride_'+key,False):result['base_properties'][key]=value
    return result


def textures(tag, materials):
    out=WORK/'TextureRuns'/tag;out.mkdir(parents=True,exist_ok=False)
    graph=read(WORK/'MaterialRuns'/materials/'Graph.json')
    roots=read(WORK/'MaterialRuns'/materials/'Report.json')['roots']
    flattened={p:flatten(graph,p) for p in roots}
    requested={p for m in flattened.values() for p in m['textures'].values() if p and p.startswith('/Game/')}
    cache=existing_exports('texture');pending=sorted(requested-set(cache));missing=[];failures=[];metadata={}
    db=sqlite3.connect((BASE/'catalog.sqlite').resolve().as_uri()+'?mode=ro',uri=True)
    from source_packages import package_key
    for start in range(0,len(pending),12):
        batch=[p for p in pending[start:start+12] if db.execute('SELECT 1 FROM entries WHERE package=? LIMIT 1',(package_key(p),)).fetchone()]
        missing += sorted(set(pending[start:start+12])-set(batch))
        if not batch:continue
        folder=out/('Batch-%04d'%start);extract_recorded(batch,folder)
        parsed=run_converter('export',batch,folder/'Export',partial=True)
        results_path=folder/'Export/Parsed/ExportResults.json'
        for package in parsed:
            for obj in package.get('objects',[]):metadata[canonical(obj['object_path'])]=obj
        for row in read(results_path):
            if row['Success']:
                path=canonical(row['ObjectPath']);cache[path]={'id':identity(path),'source':path,'file':row['DiskFilePaths'][0],
                                        'files':row['DiskFilePaths'],'export_report':str(results_path)}
            else:failures.append(row)
        print('TEXTURES',start,'/',len(pending),'FAILED',len(failures),flush=True)
    db.close()
    write_json(out/'Textures.json',{p:cache[p] for p in sorted(requested) if p in cache})
    write_json(out/'Metadata.json',metadata)
    write_json(out/'Flattened.json',flattened)
    write_json(out/'Report.json',{'requested':len(requested),'available':len(requested & set(cache)),
                                 'unavailable':sorted(requested-set(cache)),'missing_packages':missing,'failures':failures})


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('task',choices=['meshes','materials','textures'])
    parser.add_argument('--tag',required=True);parser.add_argument('--selection',default='Phase7CSelect02')
    parser.add_argument('--materials',default='Phase7CMaterials01');args=parser.parse_args()
    if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):parser.error('Use a new tag')
    if args.task=='meshes':export_meshes(args.tag,args.selection)
    elif args.task=='materials':material_graph(args.tag,args.selection)
    else:textures(args.tag,args.materials)
