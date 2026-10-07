"""Shared native shaders for documented visual reconstruction recipes."""
import hashlib
import json
import re
import unreal
from restoration_common import PROFILE

SHADER_VERSION=PROFILE['material_revision']


def shader_code(count):
    code=[]
    for i in range(count):
        code.append(f'''
float2 uv{i} = UV * UV{i}.xy + UV{i}.zw;
float4 b{i} = Texture2DSample(Base{i}, Base{i}Sampler, uv{i});
float4 n{i} = Texture2DSample(Normal{i}, Normal{i}Sampler, uv{i});
float4 p{i} = Texture2DSample(Packed{i}, Packed{i}Sampler, uv{i});
float2 xy{i} = (n{i}.rg * 2 - 1) * Layer{i}.y;
float3 normal{i} = normalize(float3(xy{i}, sqrt(max(.001, 1-dot(xy{i},xy{i})))));
float rough{i} = p{i}.g;
float metal{i} = p{i}.r;
float ao{i} = p{i}.b;
if (Layer{i}.x > .5 && Layer{i}.x < 1.5) {{ rough{i}=n{i}.b; metal{i}=0; ao{i}=b{i}.a; }}
if (Layer{i}.x > 1.5 && Layer{i}.x < 2.5) {{ rough{i}=n{i}.b; metal{i}=p{i}.r; ao{i}=p{i}.g; }}
if (Layer{i}.x > 2.5 && Layer{i}.x < 3.5) {{ rough{i}=n{i}.b; metal{i}=n{i}.a; ao{i}=b{i}.a; }}
if (Layer{i}.x > 3.5) {{ rough{i}=n{i}.a; metal{i}=0; ao{i}=n{i}.b; }}
if (Layer{i}.x > 4.5) {{ rough{i}=n{i}.b; metal{i}=0; ao{i}=1; }}
rough{i}=saturate(rough{i}*Layer{i}.z);
metal{i}=saturate(metal{i}*Layer{i}.w);
float3 color{i}=b{i}.rgb*Tint{i}.rgb;
''')
    if count==1:code.append('float3 weights=float3(1,0,0);')
    else:
        code.append('float4 sourceMask=Texture2DSample(BlendMask,BlendMaskSampler,UV*MaskUV.xy+MaskUV.zw); float3 mask=lerp(sourceMask.rgb,VertexColor.rgb,VertexBlend);')
        if count==2:code.append('float3 weights=float3(1-saturate(mask.g),saturate(mask.g),0);')
        elif count==3:code.append('float3 weights=max(mask,0); weights=dot(weights,float3(1,1,1))>.001?weights/dot(weights,float3(1,1,1)):float3(1,0,0);')
        elif count==4:code.append('float4 weights=max(sourceMask,0); weights=dot(weights,float4(1,1,1,1))>.001?weights/dot(weights,float4(1,1,1,1)):float4(1,0,0,0);')
        else:raise ValueError('Unsupported reconstructed layer count')
    def blend(field):return '+'.join(f'{field}{i}*weights.{"rgba"[i]}' for i in range(count))
    code += [f'float3 color={blend("color")};',f'OutNormal=normalize({blend("normal")});',
             f'OutRoughness={blend("rough")};',f'OutMetallic={blend("metal")};',f'OutAO=saturate({blend("ao")});']
    code.append('''
float4 overlay=Texture2DSample(Overlay,OverlaySampler,UV*OverlayUV.xy+OverlayUV.zw);
color=lerp(color,overlay.rgb,saturate(overlay.a*OverlayStrength));
float2 macroXY=(Texture2DSample(MacroNormal,MacroNormalSampler,UV).rg*2-1)*MacroStrength;
float2 detailXY=(Texture2DSample(DetailNormal,DetailNormalSampler,UV*DetailTiling).rg*2-1)*DetailStrength;
OutNormal=normalize(float3(OutNormal.xy+macroXY+detailXY,OutNormal.z));
float alpha=b0.a;
if(OpacitySource>1.5 && OpacitySource<2.5) alpha=p0.b;
if(OpacitySource>2.5) alpha=Texture2DSample(OpacityTexture,OpacityTextureSampler,UV).r;
OutOpacity=saturate(alpha*Opacity);
float3 lightColor=lerp(EmissionTint.rgb,CPDColor.rgb,UseCPD);
float lightPower=lerp(EmissionIntensity,CPDPower,UseCPD);
OutEmission=Texture2DSample(Emission,EmissionSampler,uv0).rgb*lightColor*lightPower;
return max(color,0);
''')
    # Use UE's shared wrap sampler, as regular TextureSample nodes do, so layered
    # source materials do not exceed the hardware's independent sampler limit.
    return re.sub(r'\b([A-Za-z][A-Za-z0-9]*)Sampler\b',r'GetMaterialSharedSampler(\1Sampler,View.MaterialTextureBilinearWrapedSampler)','\n'.join(code))


def create_materials(g):
    lib,tools,edit=g['LIB'],g['TOOLS'],g['EDIT']
    state,report,manifest=g['state'],g['report'],g['MANIFEST']
    dest=g['DEST']+'/Materials';masters={}
    overrides=g['read'](g['ROOT']/'Scripts/SpaceCenter/restoration_overrides.json')

    def texture(key):
        entry=manifest['texture_bindings'][key]
        return lib.load_asset(state['texture:'+entry['source']+':'+entry['role']]['asset'])

    def default_texture(name,role):return texture('calibration:'+name+':'+role)

    def expression(mat,cls,**props):
        node=edit.create_material_expression(mat,cls)
        for key,value in props.items():node.set_editor_property(key,value)
        return node

    def custom_struct(cls,**props):
        value=cls()
        for name,content in props.items():value.set_editor_property(name,content)
        return value

    def master(count,mode,two_sided):
        name=f'M_SC_R{SHADER_VERSION}_{count}_{mode}'+('_TwoSided' if two_sided else '')
        if name in masters:return masters[name]
        path=dest+'/'+name;code=shader_code(count)
        signature=hashlib.sha256(code.encode()).hexdigest();key='restoration_master:'+name
        if lib.does_asset_exist(path) and state.get(key,{}).get('sha256')==signature:
            mat=lib.load_asset(path);masters[name]=mat;return mat
        if lib.does_asset_exist(path):raise RuntimeError('Existing shader needs explicit revision: '+path)
        mat=tools.create_asset(name,dest,unreal.Material,unreal.MaterialFactoryNew())
        mat.set_editor_property('two_sided',two_sided)
        mat.set_editor_property('used_with_instanced_static_meshes',True)
        mat.set_editor_property('used_with_nanite',mode in ['opaque','masked'])
        mat.set_editor_property('blend_mode',{'opaque':unreal.BlendMode.BLEND_OPAQUE,
            'masked':unreal.BlendMode.BLEND_MASKED,'translucent':unreal.BlendMode.BLEND_TRANSLUCENT,
            'additive':unreal.BlendMode.BLEND_ADDITIVE}[mode])
        if mode=='additive':mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
        if mode=='translucent':mat.set_editor_property('translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE)
        inputs={}
        inputs['UV']=expression(mat,unreal.MaterialExpressionTextureCoordinate)
        inputs['VertexColor']=expression(mat,unreal.MaterialExpressionVertexColor)
        for i in range(count):
            for prefix,default,role in [('Base','white','color'),('Normal','normal','normal_data'),('Packed','pbr','data')]:
                n=prefix+str(i)
                inputs[n]=expression(mat,unreal.MaterialExpressionTextureObjectParameter,parameter_name=n,texture=default_texture(default,role),
                    sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if role=='color' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
            for prefix,value in [('UV',(1,1,0,0)),('Tint',(1,1,1,1)),('Layer',(0,1,1,1))]:
                n=prefix+str(i);inputs[n]=expression(mat,unreal.MaterialExpressionVectorParameter,parameter_name=n,default_value=unreal.LinearColor(*value))
        for name,default,role in [('BlendMask','black','data'),('Overlay','white','color'),('MacroNormal','normal','normal_data'),('DetailNormal','normal','normal_data'),('Emission','black','color'),('OpacityTexture','white','data')]:
            inputs[name]=expression(mat,unreal.MaterialExpressionTextureObjectParameter,parameter_name=name,texture=default_texture(default,role),
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if role=='color' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        for name,value in [('MaskUV',(1,1,0,0)),('OverlayUV',(1,1,0,0)),('EmissionTint',(1,1,1,1))]:
            inputs[name]=expression(mat,unreal.MaterialExpressionVectorParameter,parameter_name=name,default_value=unreal.LinearColor(*value))
        for name,value in [('VertexBlend',0),('OverlayStrength',0),('MacroStrength',0),('DetailStrength',0),('DetailTiling',1),
                           ('EmissionIntensity',0),('OpacitySource',1),('Opacity',1),('UseCPD',0)]:
            inputs[name]=expression(mat,unreal.MaterialExpressionScalarParameter,parameter_name=name,default_value=value)
        inputs['CPDColor']=expression(mat,unreal.MaterialExpressionVectorParameter,parameter_name='CPDColor',default_value=unreal.LinearColor(1,1,1,1),
                                      use_custom_primitive_data=True,primitive_data_index=0)
        inputs['CPDPower']=expression(mat,unreal.MaterialExpressionScalarParameter,parameter_name='CPDPower',default_value=1.,
                                      use_custom_primitive_data=True,primitive_data_index=3)
        outputs=[('OutNormal',unreal.CustomMaterialOutputType.CMOT_FLOAT3,unreal.MaterialProperty.MP_NORMAL),
                 ('OutRoughness',unreal.CustomMaterialOutputType.CMOT_FLOAT1,unreal.MaterialProperty.MP_ROUGHNESS),
                 ('OutMetallic',unreal.CustomMaterialOutputType.CMOT_FLOAT1,unreal.MaterialProperty.MP_METALLIC),
                 ('OutAO',unreal.CustomMaterialOutputType.CMOT_FLOAT1,unreal.MaterialProperty.MP_AMBIENT_OCCLUSION),
                 ('OutEmission',unreal.CustomMaterialOutputType.CMOT_FLOAT3,unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                 ('OutOpacity',unreal.CustomMaterialOutputType.CMOT_FLOAT1,unreal.MaterialProperty.MP_OPACITY_MASK if mode=='masked' else unreal.MaterialProperty.MP_OPACITY)]
        custom=expression(mat,unreal.MaterialExpressionCustom,code=code,description='Phase7C visual reconstruction; original graph unavailable',
                          output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                          inputs=[custom_struct(unreal.CustomInput,input_name=n) for n in inputs],
                          additional_outputs=[custom_struct(unreal.CustomOutput,output_name=n,output_type=t) for n,t,p in outputs])
        for name,node in inputs.items():
            # A vector parameter's first output is RGB; append A for packed four-component controls.
            if isinstance(node,unreal.MaterialExpressionVectorParameter):
                append=expression(mat,unreal.MaterialExpressionAppendVector)
                assert edit.connect_material_expressions(node,'RGB',append,'A')
                assert edit.connect_material_expressions(node,'A',append,'B')
                node=append
            assert edit.connect_material_expressions(node,'',custom,name),name
        assert edit.connect_material_property(custom,'',unreal.MaterialProperty.MP_BASE_COLOR)
        for n,t,prop in outputs:
            if n!='OutOpacity' or mode!='opaque':assert edit.connect_material_property(custom,n,prop),n
        spec=expression(mat,unreal.MaterialExpressionScalarParameter,parameter_name='Specular',default_value=.5)
        assert edit.connect_material_property(spec,'',unreal.MaterialProperty.MP_SPECULAR)
        edit.layout_material_expressions(mat);edit.recompile_material(mat);lib.save_loaded_asset(mat)
        state[key]={'asset':path,'sha256':signature};masters[name]=mat
        return mat

    def vector(mi,name,value):edit.set_material_instance_vector_parameter_value(mi,name,unreal.LinearColor(*value))
    def scalar(mi,name,value):edit.set_material_instance_scalar_parameter_value(mi,name,float(value))
    def tex(mi,name,key):edit.set_material_instance_texture_parameter_value(mi,name,texture(key))

    report['materials']=[]
    for index,(path,r) in enumerate(manifest['materials'].items()):
        if path in overrides['materials']:
            native=overrides['materials'][path];assert lib.does_asset_exist(native)
            state['restoration_material:'+path]={'asset':native,'manual_override':True};continue
        signature=hashlib.sha256(json.dumps([SHADER_VERSION,r],sort_keys=True).encode()).hexdigest()
        name=('MI_SC_R_' if SHADER_VERSION==1 else f'MI_SC_R{SHADER_VERSION}_')+r['id'];native=dest+'/'+name;key='restoration_material:'+path
        if lib.does_asset_exist(native) and state.get(key,{}).get('sha256')==signature:
            report['reused']+=1;continue
        mi=lib.load_asset(native) if lib.does_asset_exist(native) else tools.create_asset(name,dest,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(mi,master(len(r['layers']),r['mode'],r['two_sided']))
        for i,layer in enumerate(r['layers']):
            for n,k in [('Base','base'),('Normal','normal'),('Packed','packed')]:tex(mi,n+str(i),layer[k])
            vector(mi,'UV'+str(i),layer['uv']);vector(mi,'Tint'+str(i),layer['tint'])
            vector(mi,'Layer'+str(i),[layer['mode'],layer['normal_strength'],layer['roughness'],layer['metallic']])
        for n,k in [('BlendMask','blend_mask'),('Overlay','overlay'),('MacroNormal','macro_normal'),('DetailNormal','detail_normal'),('Emission','emissive'),('OpacityTexture','opacity_texture')]:tex(mi,n,r[k])
        for n,k in [('OverlayUV','overlay_uv'),('EmissionTint','emission_tint')]:vector(mi,n,r[k])
        vector(mi,'MaskUV',r.get('mask_uv',[1,1,0,0]))
        for n,k in [('VertexBlend','vertex_blend'),('OverlayStrength','overlay_strength'),('MacroStrength','macro_strength'),('DetailStrength','detail_strength'),
                    ('DetailTiling','detail_tiling'),('EmissionIntensity','emission_intensity'),('OpacitySource','opacity_source'),('Opacity','opacity'),('Specular','specular'),('UseCPD','use_cpd')]:scalar(mi,n,r[k])
        edit.update_material_instance(mi);lib.save_loaded_asset(mi)
        state[key]={'asset':native,'sha256':signature};report['materials'].append(native)
        g['write_json'](g['STATE_FILE'],state)
        if index%30==0:
            unreal.SystemLibrary.collect_garbage();print('RESTORE_MATERIAL',index,'/',len(manifest['materials']),flush=True)
