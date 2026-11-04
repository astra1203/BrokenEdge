import unreal, json
L=lambda s: unreal.log('PROF '+str(s))
def prof(path, axis):
    m=unreal.load_asset(path)
    v=json.loads(unreal.StingerAssetLibrary.describe_skeletal_vertices(m))
    L('== %s n=%d'%(path,len(v)))
    n=m.num_sockets()
    for i in range(n):
        s=m.get_socket_by_index(i); L(' sock %s bone=%s loc=%s rot=%s'%(s.socket_name,s.bone_name,s.relative_location.to_tuple() if hasattr(s.relative_location,'to_tuple') else s.relative_location,s.relative_rotation))
    if not v: return
    lo=min(p[axis] for p in v); hi=max(p[axis] for p in v)
    st=5
    x=lo
    while x<hi:
        sl=[p for p in v if x<=p[axis]<x+st]
        if sl: L(' %s[%6.1f,%6.1f) z %6.1f..%6.1f  lat %5.1f..%5.1f'%('xy'[axis],x,x+st,min(p[2] for p in sl),max(p[2] for p in sl),min(p[1-axis] for p in sl),max(p[1-axis] for p in sl)))
        x+=st
prof('/Game/MilitaryWeapSilver/Weapons/Sniper_Rifle_A',0)
prof('/Game/MilitaryWeapSilver/Weapons/Shotgun_A',0)
prof('/Game/Characters/Stinger/Meshes/FirstPerson/ViewFit/SK_KA47_ViewFit',1)
a=unreal.load_asset('/Game/Characters/Stinger/Animations/DeltaForceLocomotion/DA_AKLocomotionPresentation')
f=a.get_editor_property('view_fit')
L('fit enabled=%s hip=%s aim=%s'%(f.get_editor_property('enabled'),f.get_editor_property('hip'),f.get_editor_property('aim')))
