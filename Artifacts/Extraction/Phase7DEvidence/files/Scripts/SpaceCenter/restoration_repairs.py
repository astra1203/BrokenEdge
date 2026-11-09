"""Bounded source-parameter repairs; every approximation stays in the material ledger."""


def repair(r,source,bind):
    textures,scalars,vectors=source['textures'],source['scalars'],source['vectors']
    layer=r['layers'][0];parent=r['source_parent'];notes=r['notes']
    def color(name):return [vectors[name].get(k,1) for k in 'RGBA']
    if r['source'].endswith('/MI_Industrial_Mechanical_LiquidTank_01') and layer['base'].startswith('calibration:'):
        donor='/Game/Environment/Props/Industrial/Mechanical/00_Textures/HD/T_Industrial_Mechanical_ConcreteSilo_03A_04_C_HD'
        layer['base']=bind(donor,'color','white','layer0.base')
        notes.append('Substitution: null LiquidTank base uses '+donor+' from the same ConcreteSilo_03A_04 surface family as its retained source NRM; no original base equivalence claimed.')
    if parent=='M_EFX_RandomQuad' and 'SelecetColor' in vectors:
        selected=color('SelecetColor')
        r.update(mode='additive',opacity=selected[3],opacity_source=1,
                 emissive=bind(None,'color','white','emissive'),emission_tint=selected,emission_intensity=1)
        layer['tint']=selected
        notes.append('Retained source additive mode and SelecetColor RGBA. Static illuminated panel approximation; procedural quad animation remains unavailable.')
    if parent=='M_Puddle_PlanDecal':
        layer['tint']=color('WaterColor')
        r.update(mode='translucent',opacity=scalars.get('Opcaity',1),opacity_source=3,
                 opacity_texture=bind(textures.get('Puddle'),'data','white','opacity'))
        layer['roughness']=scalars.get('RoughnessMax',.06)/(166/255)
        notes.append('Puddle mask drives transparency with retained WaterColor/Opcaity/RoughnessMax; lost scene-depth/refraction wiring approximated.')
    if 'PoolWater' in parent and 'ScatteringColor' in vectors:
        layer['tint']=color('ScatteringColor')
        layer['roughness']=scalars.get('Roughness',.05)/(166/255)
        r.update(mode='translucent',opacity=scalars.get('TranslucentIntensity',.333333))
        notes.append('Static water uses retained ScatteringColor, Roughness and TranslucentIntensity; custom alpha-composite, depth and wave graph approximated.')
    if parent=='M_VariousGlassMaster-Basic' and 'Glass Base Color' in vectors:
        layer['tint']=color('Glass Base Color')
        layer['roughness']=scalars.get('Glass Roughness',.05)/(166/255)
        if source['switches'].get('- Glass Normal? -') is False:layer['normal_strength']=0
        r['opacity']=max(0,min(1,scalars.get('Transparency A [Inner]',.22)*scalars.get('Transparency [Overall]',1)))
        notes.append('Glass base tint, disabled-normal switch and inner/overall transparency retained; view-dependent edge/refraction graph approximated.')
    if parent=='M_Brokenglass' and 'ColorThick' in vectors:
        layer['tint']=color('ColorThick');layer['roughness']=scalars.get('Roughness',.05)/(166/255)
        r['opacity']=scalars.get('Opacity Top',.2)*scalars.get('Opacity Control',1)
        notes.append('Broken glass uses retained ColorThick/Roughness/Opacity Top/Opacity Control; original thickness/refraction graph approximated.')
