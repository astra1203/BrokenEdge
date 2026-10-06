"""Explicit, reviewable reconstruction recipes; source shader graphs remain stripped."""
import argparse
import collections
import struct
import zlib
from restoration_common import *
from restore_assets import flatten

CALIBRATION={'white':(255,255,255,255),'black':(0,0,0,255),
             'normal':(128,128,255,255),'pbr':(0,166,255,255)}


def calibration(name):
    path=WORK/'Calibration'/(name+'.png')
    def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b))
    data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',4,4,8,6,0,0,0))
    data+=chunk(b'IDAT',zlib.compress((b'\0'+bytes(CALIBRATION[name])*4)*4))+chunk(b'IEND',b'')
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists():path.write_bytes(data)
    if path.read_bytes()!=data:raise RuntimeError('Calibration changed unexpectedly')
    return {'source':'calibration:'+name,'file':str(path)}


def make_recipes(graph,roots,textures,substitutions=None,material_variants=None):
    bindings={};recipes={};missing=[]
    substitutions=substitutions or {}
    material_variants=material_variants or {}

    def bind(path,role,default,material,parameter):
        source=path
        if path and path not in textures:path=substitutions.get(path,{}).get('replacement')
        entry=textures.get(path) if path else None
        if not entry:
            if source:missing.append({'material':material,'parameter':parameter,'texture':source,'default':default})
            entry=calibration(default)
        key=entry['source']+':'+role
        bindings[key]=entry|{'role':role}
        return key

    for path in sorted(roots):
        recipe_source=material_variants.get(path,path)
        m=flatten(graph,recipe_source);t=m['textures'];s=m['scalars'];v=m['vectors'];sw=m['switches']
        def tex(*names):
            for name in names:
                choice=name+'_SVT' if sw.get('UseSVT') and t.get(name+'_SVT') else name
                if t.get(choice):return t[choice]
            return None
        def scalar(names,default):
            for n in names:
                if isinstance(s.get(n),(int,float)):return s[n]
            return default
        def vector(names,default):
            for n in names:
                if isinstance(v.get(n),dict):return [v[n].get(c,default[i]) for i,c in enumerate('RGBA')]
            return list(default)
        parent=m['chain'][0].split('/')[-1] if m['chain'] else 'Unknown'
        foliage='SpeedTree' in parent
        impostor=foliage and sw.get('IsImpostor',False)
        notes=[];layers=[]
        layered=bool(tex('01_R_Base','01_Base','BaseLayer_BaseColor_Height'))
        rock=bool(tex('BaseLayer_BaseColor_Height'))
        count=(3 if sw.get('Three Layers?',bool(tex('03_Base'))) else 2) if layered else 1
        if rock:count=3 if sw.get('Use Layer G') else 2 if sw.get('Use Layer R') else 1
        for i in range(count):
            prefix=['01_R','02_G','03_B'][i]
            if rock:
                prefix=['BaseLayer','R','G'][i]
                base=tex(prefix+'_BaseColor_Height');normal=tex(prefix+'_NAR');packed=None;mode=4
                uv=[scalar([['Tiling','R_Tiling','G_Tiling'][i]],1),]*2+[0,0]
                if i==0:uv=vector(['UVScale_Offset'],[1,1,0,0])
                tint=vector(['Layer%dTint'%(i+1)],[1,1,1,1])
            elif layered:
                base=tex(prefix+'_Base','0%d_Base'%(i+1));normal=tex(prefix+'_Normal','0%d_Normal'%(i+1))
                packed=tex(prefix+'_MRA');mode=0
                uv=[scalar(['Texcoord_'+prefix,'0%d_Tiling'%(i+1)],1)]*2+[0,0]
                tint=vector(['BaseColorScaler_'+prefix,'0%d_Color'%(i+1)],[1,1,1,1])
            else:
                base=tex('BaseColorMap','BAMap','BaseColor','BaseColorOpacity','ColorAo','BaseTex','DecalTexture',
                         'BaseColorTex_PC','WindowFrame Diffuse','PatternA','Backround','Base')
                normal=tex('NormalMap','NRMap','NRMMap','NormalMaskTransmittance','NormalTransmittance','NRAMap',
                           'Normal','Glass Normal','- Glass Normal -','-- Mesh Normal --','GlassNormalMap',
                           'NormalTexture','NormalDecalTex','NormalTex_PC','WindowFrame Normal')
                packed=tex('MRAMap','MACMap','Metalic_Roughness_Alpha_Map','Metalic_Roughness_Alpha_Tex_PC','WindowFrame MetalRoughAmbient')
                mode=2 if tex('MACMap') else 3 if tex('NRMMap') else 1 if tex('NRMap') else 4 if tex('NRAMap') else 0
                uv=vector(['UVScale_Offset'],[scalar(['Texcoord','UVScale'],1)]*2+[0,0])
                tint=vector(['BaseColorScaler','BaseColor','Color','TintColor','Color_01'],[1,1,1,1])
                if foliage:
                    # Both paths are inherited by SpeedTree instances. Only the
                    # source static switch selects the active atlas; choosing
                    # BaseColorOpacity first makes ordinary leaves default white.
                    base=tex('BaseColorOpacity') if impostor else tex('ColorAo')
                    normal=tex('NormalTransmittance') if impostor else tex('NormalMaskTransmittance')
                    packed=None;mode=0
            layers.append({'base':bind(base,'color','white',path,'layer%d.base'%i),
                           'normal':bind(normal,'normal_data','normal',path,'layer%d.normal'%i),
                           'packed':bind(packed,'data','pbr',path,'layer%d.packed'%i),
                           'uv':uv,'tint':tint,'mode':mode,
                           'normal_strength':scalar(['NormalIntensity','NormalScale'],1) if normal else 0,
                           'roughness':scalar(['RoughnessScale','RoughnessIntensity'],1),
                           'metallic':scalar(['MetallicScale','MetallicIntensity'],1),
                           'has_packed':bool(packed),'has_normal':bool(normal)})
            if foliage:
                layers[-1]['roughness']=scalar(['TrunkRoughness' if sw.get('IsOpaque') else 'LeafRoughness'],.65)/(166/255)
                layers[-1]['metallic']=0
        blend=str(m['base_properties'].get('BlendMode',''))
        glass=any(k.lower() in parent.lower() for k in ['Glass','Translucent','TransparentCloth','Pool','FlowMap'])
        decal='Decal' in parent
        mode='translucent' if glass else 'masked' if decal or foliage or 'Mask' in blend or 'OpacityMask' in parent else 'opaque'
        if foliage and sw.get('IsOpaque'):mode='opaque'
        if 'Translucent' in blend and not decal:mode='translucent'
        opacity_source=2 if decal and tex('Metalic_Roughness_Alpha_Map','Metalic_Roughness_Alpha_Tex_PC') else 3 if foliage and not impostor and tex('Opacity') else 1
        opacity=scalar(['Opacity','Alpha','OpacityMaskMax'],.22 if glass else 1)
        overlay=tex('Overlay_ColorMap');overlay_enabled=bool(sw.get('UseOverlay',sw.get('UseOverlay?',bool(overlay))))
        emission=tex('EmissiveMap','EMMap','EmissiveMap2')
        if not emission and any(k in parent for k in ['Screen','Brand','Spectrum']):emission=tex('BaseColor','BaseColorMap','PatternA','Backround')
        intensity=scalar(['EmissiveIntensity','EmissivePower','LightPower','EmissionIntensity','Emission'],1 if emission else 0)
        recipe={'id':identity(path),'source':path,'source_parent':parent,'mode':mode,'layers':layers,
                'two_sided':bool(m['base_properties'].get('TwoSided',foliage or decal or glass)),
                'blend_mask':bind(tex('BlendMask','MaskMap'),'data','black',path,'blend_mask'),
                'vertex_blend':layered and not tex('BlendMask','MaskMap'),
                'overlay':bind(overlay,'color','white',path,'overlay'),
                'overlay_strength':scalar(['Overlay_Opcaity','Overlay_Opacity','OverlayIntensity'],1) if overlay_enabled else 0,
                'overlay_uv':vector(['Overlay_UVScale_Offset'],[1,1,0,0]),
                'macro_normal':bind(tex('NormalMap') if layered and not rock else None,'normal_data','normal',path,'macro_normal'),
                'macro_strength':1 if layered and not rock and tex('NormalMap') else 0,
                'detail_normal':bind(tex('DetailNormalMap','DetailNormal','DetailMap'),'normal_data','normal',path,'detail_normal'),
                'detail_strength':scalar(['DetailNormalScale','DetailNormalIntensity'],.3) if tex('DetailNormalMap','DetailNormal','DetailMap') else 0,
                'detail_tiling':scalar(['DetailNormalTiling'],5),
                'emissive':bind(emission,'color','black',path,'emissive'),
                'emission_tint':vector(['EmissiveColor','EmissionColor','LightColor'],[1,1,1,1]),
                'emission_intensity':intensity,'use_cpd':'CPD' in parent,
                'opacity_texture':bind(tex('Opacity'),'data','white',path,'opacity'),
                'opacity_source':opacity_source,'opacity':opacity if mode!='opaque' else 1,
                'specular':scalar(['Specular'],.5),'source_chain':m['chain'],'notes':notes}
        if not layers[0]['base'].startswith('calibration:'):
            notes.append('Original texture and inherited parameters; lost source node wiring visually reconstructed.')
        else:notes.append('No decodable base texture selected; use retained tint pending visual review.')
        if rock:notes.append('Rock layer blending reconstructed; source RVT/global tint unavailable.')
        if 'EFX' in parent or 'Screen' in parent or 'Water' in parent or 'Pool' in parent:
            notes.append('Static appearance reconstruction; original effect animation graph not recovered.')
        recipes[path]=recipe
        if recipe_source!=path:
            recipe['source_variant']=recipe_source
            notes.append('Same-object source HD material used because the referenced legacy/generated images are absent.')
    return recipes,bindings,missing


def main(args):
    out=WORK/'RecipeRuns'/args.tag
    graph=read(WORK/'MaterialRuns'/args.materials/'Graph.json')
    roots=read(WORK/'MaterialRuns'/args.materials/'Report.json')['roots']
    textures=read(WORK/'TextureRuns'/args.textures/'Textures.json')
    recipes,bindings,missing=make_recipes(graph,roots,textures)
    write_json(out/'Materials.json',recipes);write_json(out/'Textures.json',bindings)
    write_json(out/'Report.json',{'materials':len(recipes),'textures':len(bindings),'unavailable_bindings':missing,
                                'modes':dict(collections.Counter(r['mode'] for r in recipes.values())),
                                'boundary':'Visual reconstruction recipes; image review and native shader validation required.'})


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--tag',required=True)
    p.add_argument('--materials',default='Phase7CMaterials01');p.add_argument('--textures',default='Phase7CTextures01')
    main(p.parse_args())
