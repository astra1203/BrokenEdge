"""Build the complete deterministic native reconstruction manifest."""
import argparse
import copy
import re
import struct
import zlib
from restoration_common import *
from restoration_recipes import make_recipes,calibration


def weight_image(out):
    entries=read(WORK/'TerrainTextureRuns/Phase7CTerrainTextures01/Weights.json')
    active=[0,1,2,4];width=height=512;pixels=bytearray(width*height*4);sources=[]
    for e in entries:
        x,y=map(int,re.search(r'_x(\d+)_y(\d+)',e['name']).groups())
        data=Path(e['raw_file']).read_bytes();sources.append({'source':e['object_path'],'sha256':sha(e['raw_file'])})
        for i,(value,) in enumerate(struct.iter_unpack('<H',data)):
            first=(value>>8)>>3;second=(value&255)>>3;blend=(value&7)/7
            assert ((value>>8)&7)==0 and first in active and second in active
            weights=[0,0,0,0];weights[active.index(first)]=round((1-blend)*255)
            weights[active.index(second)]+=round(blend*255)
            assert sum(weights)==255
            offset=(((y-10)*128+i//128)*width+(x-21)*128+i%128)*4
            pixels[offset:offset+4]=bytes(weights)
    def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b))
    scanlines=b''.join(b'\0'+pixels[i*width*4:(i+1)*width*4] for i in range(height))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(scanlines))+chunk(b'IEND',b'')
    target=out/'SourceTerrainWeights.png';target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(png)
    write_json(out/'TerrainWeightReconstruction.json',{'sources':sources,'active_layers':active,
        'source_origin_cm':[646800,-476800],'size_cm':[51200,51200],
        'method':'Observed G16 bit fields: high-byte layer index >>3, low-byte layer index >>3, low 3 bits /7 blend. All observed values validated. Shader interpolation visually reconstructed.',
        'source_shader_verified':False,'image_sha256':sha(target)})
    return {'source':'derived:SpaceCenter-original-weightmap-v1','file':str(target),'role':'data'}


def main(tag):
    out=WORK/'ManifestRuns'/tag
    graph=read(WORK/'AugmentRuns/Phase7CAugment01/Graph.json')
    textures=read(WORK/'AugmentRuns/Phase7CAugment01/Textures.json')
    roots=read(WORK/'MaterialRuns/Phase7CMaterials02/Report.json')['roots']
    variants=read(ROOT/'Scripts/SpaceCenter/restoration_source_variants.json')['material_variants']
    recipes,bindings,missing=make_recipes(graph,roots,textures,material_variants=variants)
    if missing:raise RuntimeError('Selected material recipe has missing images: '+str(missing))
    terrain_report=read(WORK/'TerrainTextureRuns/Phase7CTerrainTextures01/Report.json')
    arrays=terrain_report['arrays'];layers=[]
    parameters=next(o['Properties']['LayerParameters'] for p in read(BASE/'Phase7B/SourceRuns/Phase7BSource01/Parsed/Packages.json')
                    for o in p.get('objects',[]) if o.get('Type')=='DTAComponent')
    for i in [0,1,2,4]:
        entries={}
        for key,array,role in [('base','TAHD_Iris_C_DTA_0','color'),('normal','TAHD_Iris_N_DTA_1','normal_data')]:
            source=arrays[array][i];binding=source+':'+role
            bindings[binding]=textures[source]|{'role':role};entries[key]=binding
        entries.update(packed='calibration:pbr:data',uv=[parameters[i]['TillingParameter']]*2+[0,0],tint=[1,1,1,1],
                       mode=5,normal_strength=1,roughness=1,metallic=0,has_packed=False,has_normal=True)
        layers.append(entries)
    weights=weight_image(out);weight_key=weights['source']+':data';bindings[weight_key]=weights
    terrain=copy.deepcopy(next(r for r in recipes.values() if r['mode']=='opaque' and len(r['layers'])==1))
    terrain_source='terrain:SpaceCenter-source-layers'
    terrain.update(id=identity(terrain_source),source=terrain_source,source_parent='Original terrain DTA parameters with reconstructed blend',layers=layers,
        two_sided=False,blend_mask=weight_key,vertex_blend=False,mask_uv=[1/512,1/512,(ORIGIN[0]-646800)/51200,(ORIGIN[1]+476800)/51200],
        overlay_strength=0,macro_strength=0,detail_strength=0,emission_intensity=0,use_cpd=False,opacity=1,
        source_chain=['/Game/Maps/BuildMaps_HD/SpaceCenter_Ma1/Runtime/LandscapeCDLOD/LandscapeCDLOD'],
        notes=['Source height and weight samples; terrain shader visually reconstructed. Missing shared Rock01 image uses same-name original Forrest_Tutorial asset.',
               'Lost global landscape tint and original virtual-texture shader graph are not claimed recovered.'])
    recipes[terrain_source]=terrain
    scene=read(WORK/'SelectionRuns/Phase7CSelect05/Selection.json')
    scene['meshes']={p:stage_import(e) for p,e in read(WORK/'AssetRuns/Phase7CMeshes02/Meshes.json').items()}
    scene['terrain']=read(WORK/'TerrainRuns/Phase7CTerrain01/Manifest.json')
    scene['terrain']['tiles']=[stage_import(e)|{'material':terrain_source} for e in scene['terrain']['tiles']]
    scene.update(version=3,materials=recipes,texture_bindings={k:stage_import(e) for k,e in sorted(bindings.items())},
                 reconstruction_scope='Visual reconstruction of original-source version; source shader equivalence not claimed.')
    write_json(out/'Manifest.json',scene)
    write_json(out/'Report.json',{'actors':len(scene['actors']),'materials':len(recipes),'textures':len(bindings),
        'meshes':len(scene['meshes']),'terrain_tiles':len(scene['terrain']['tiles']),'manifest_sha256':sha(out/'Manifest.json')})
    print(read(out/'Report.json'),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--tag',required=True);main(p.parse_args().tag)
