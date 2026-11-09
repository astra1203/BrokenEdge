"""Resolve explicit same-object material variants and missing shared terrain images."""
import argparse
from restoration_common import *
from restore_assets import flatten


def main(tag):
    out=WORK/'AugmentRuns'/tag
    variants=read(ROOT/'Scripts/SpaceCenter/restoration_source_variants.json')['material_variants']
    extract_recorded(variants.values(),out/'Materials')
    graph=read(WORK/'MaterialRuns/Phase7CMaterials02/Graph.json')
    for p in run_converter('inspect',variants.values(),out/'Materials/Inspect'):
        graph[p['package']]=p
    terrain=read(WORK/'TerrainTextureRuns/Phase7CTerrainTextures01/Report.json')
    aliases={p:p.replace('/SpaceCenter_Ma1/','/Forrest_Tutorial/') for p in terrain['missing_textures']}
    requested={p for path in variants.values() for p in flatten(graph,path)['textures'].values() if p}
    requested.update(aliases.values())
    textures=read(WORK/'TextureRuns/Phase7CTextures02/Textures.json')
    names=sorted(requested-set(textures))
    for start in range(0,len(names),12):
        batch=names[start:start+12];folder=out/('Textures-%03d'%start)
        extract_recorded(batch,folder)
        run_converter('export',batch,folder/'Export')
        for r in read(folder/'Export/Parsed/ExportResults.json'):
            if not r['Success']:raise RuntimeError('Source variant texture failed')
            p=canonical(r['ObjectPath']);textures[p]={'id':identity(p),'source':p,'file':r['DiskFilePaths'][0],
                                                      'export_report':str(folder/'Export/Parsed/ExportResults.json')}
    for r in read(WORK/'TerrainTextureRuns/Phase7CTerrainTextures01/Textures/Export/Parsed/ExportResults.json'):
        if not r['Success']:raise RuntimeError('Original terrain texture failed')
        p=canonical(r['ObjectPath']);textures[p]={'id':identity(p),'source':p,'file':r['DiskFilePaths'][0]}
    for original,variant in aliases.items():
        textures[original]=textures[variant]|{'source':original,'substitution':variant,
            'reason':'Same named shared terrain image from original Forrest_Tutorial source; SpaceCenter copy absent.'}
    write_json(out/'Graph.json',graph);write_json(out/'Textures.json',textures)
    write_json(out/'Report.json',{'material_variants':variants,'terrain_texture_variants':aliases,'new_exports':len(names)})


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--tag',required=True);main(p.parse_args().tag)
