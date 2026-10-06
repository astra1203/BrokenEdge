"""Preserve targeted source probes separately from production assets."""
import argparse
from restoration_common import *


def probe(tag):
    out=WORK/'ProbeRuns'/tag
    inventory=read(WORK/'DiscoveryRuns/Phase7CDiscover01/PackageInventory.json')
    names=read(WORK/'TemplatePackages.json')
    runtime='/Game/Maps/BuildMaps_HD/SpaceCenter_Ma1/Runtime/'
    names += [runtime+g+'/'+g+'Streamer' for g in ['Tree','Bushes','Grass','DecorationFoliage']]
    names += [p['package'] for p in inventory if p['package'].endswith(('Tree_X52_Y-36','Bushes_X52_Y-36','Grass_X104_Y-71'))]
    names += [runtime+'LandscapeCDLOD/'+n for n in ['TAHD_Iris_C_DTA_0','TAHD_Iris_N_DTA_1','WeightmapClipmap/Mip0_x22_y11']]
    extraction=extract(sorted(names),limit_gib=2)
    write_json(out/'Extractions.json',extraction)
    parsed=run_converter('inspect',set(names)-set(extraction['missing']),out/'Inspect',partial=True)
    write_json(out/'Report.json',{'packages':len(parsed),'missing':extraction['missing'],
                                'errors':[p['package'] for p in parsed if p.get('error') or p.get('failures')]})


def dependencies(tag):
    out=WORK/'ProbeRuns'/tag
    prior=read(WORK/'ProbeRuns/Phase7CProbe02/Inspect/Parsed/Packages.json')
    names=set()
    for p in prior:
        for o in p.get('objects',[]):
            ref=o.get('class_reference')
            if '/Default/' in p['package'] and ref and ref['ObjectPath'].startswith('/Game/'):
                names.add(canonical(ref['ObjectPath']))
            props=o.get('Properties',{})
            if o.get('Type')=='FoliageAssetsStreamer':
                template=props.get('AssetPathTemplate',p['package']+'/'+p['package'].split('/')[-2]+'_X%d_Y%d')
                for a in props.get('Assets',[]):
                    b=a['Bounds'];center=b['Origin'];extent=b['BoxExtent']
                    if all(center[k]+extent[k]>=MINIMUM[i] and center[k]-extent[k]<=MAXIMUM[i] for i,k in enumerate('XY')):
                        names.add(template%(a['ID']['X'],a['ID']['Y']))
            if 'DynamicTexture' in o.get('Type',''):
                names.update(canonical(t['AssetPathName']) for t in props['TextureArray'][:5])
    extraction=extract(sorted(names),limit_gib=2)
    write_json(out/'Extractions.json',extraction)
    parsed=run_converter('inspect',set(names)-set(extraction['missing']),out/'Inspect',partial=True)
    write_json(out/'Report.json',{'packages':len(parsed),'missing':extraction['missing'],
                                'errors':[p['package'] for p in parsed if p.get('error') or p.get('failures')]})


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--tag',required=True);p.add_argument('--dependencies',action='store_true')
    a=p.parse_args();(dependencies if a.dependencies else probe)(a.tag)
