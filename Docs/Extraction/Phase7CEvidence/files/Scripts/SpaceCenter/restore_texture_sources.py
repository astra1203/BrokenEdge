"""Recover valid original mips and inspect original terrain weights without substitutions."""
import argparse
import collections
import sqlite3
import struct
from restoration_common import *


def recover(tag):
    out=WORK/'TextureRecoveryRuns'/tag
    failures=read(WORK/'TextureRuns/Phase7CTextures01/Report.json')['failures']
    names=sorted({canonical(r['ObjectPath']) for r in failures})
    parsed=run_converter('texture-mips',names,out/'Mips',partial=True)
    recovered={};failed=[]
    for p in parsed:
        for o in p.get('objects',[]):
            success=next((a for a in o.get('mip_attempts',[]) if a.get('file')),None)
            path=canonical(o['object_path'])
            if success:recovered[path]={'id':identity(path),'source':path,'file':success['file'],'source_mip':success['mip'],
                                        'metadata':o,'export_report':str(out/'Mips/Parsed/Packages.json')}
            else:failed.append(path)
    write_json(out/'Textures.json',recovered);write_json(out/'Report.json',{'recovered':len(recovered),'failed':failed})
    if failed:run_converter('inspect',failed[:3],out/'InspectFailures',partial=True)
    print('TEXTURE_RECOVERY',len(recovered),'FAILED',len(failed),flush=True)


def terrain(tag):
    out=WORK/'TerrainTextureRuns'/tag
    inv=read(WORK/'DiscoveryRuns/Phase7CDiscover01/PackageInventory.json')
    root='/Game/Maps/BuildMaps_HD/SpaceCenter_Ma1/Runtime/LandscapeCDLOD/'
    wanted={root+'WeightmapClipmap/Mip0_x%d_y%d'%(x,y) for x in range(21,25) for y in range(10,14)}
    available={p['package'] for p in inv};missing=sorted(wanted-available)
    if missing:raise RuntimeError('Missing region weight tiles '+str(missing))
    extract_recorded(wanted,out/'Weights')
    raw=run_converter('rawtexture',wanted,out/'Weights/Raw')
    hist=collections.Counter();weights=[]
    for p in raw:
        for o in p['objects']:
            if o.get('pixel_format')!='PF_G16':raise ValueError('Expected G16 weights')
            data=Path(o['raw_file']).read_bytes();assert len(data)==o['raw_width']*o['raw_height']*2
            hist.update(v[0] for v in struct.iter_unpack('<H',data));weights.append(o)
    probes=read(WORK/'ProbeRuns/Phase7CProbe02/Inspect/Parsed/Packages.json')
    arrays={o['Name']:[canonical(t['AssetPathName']) for t in o['Properties']['TextureArray'][:5]]
            for p in probes for o in p.get('objects',[]) if 'DynamicTexture' in o['Type']}
    names={p for a in arrays.values() for p in a}
    extraction=extract(sorted(names),limit_gib=2);write_json(out/'Textures/Extractions.json',extraction)
    run_converter('export',names-set(extraction['missing']),out/'Textures/Export',partial=True)
    write_json(out/'Weights.json',weights)
    write_json(out/'Report.json',{'weight_tiles':len(weights),'histogram':sorted(hist.items()),'arrays':arrays,'missing_textures':extraction['missing']})
    print('TERRAIN_WEIGHTS',len(weights),'UNIQUE',len(hist),'MOST_COMMON',hist.most_common(15),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('task',choices=['recover','terrain']);p.add_argument('--tag',required=True);a=p.parse_args()
    (recover if a.task=='recover' else terrain)(a.tag)
