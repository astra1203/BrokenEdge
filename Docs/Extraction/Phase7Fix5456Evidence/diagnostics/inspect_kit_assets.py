import json, math
from pathlib import Path
import unreal
out=Path(unreal.Paths.project_saved_dir())/'Extraction/Fix5456AssetReload01'
out.mkdir(parents=True,exist_ok=False)
source=unreal.load_asset('/Game/Characters/Stinger/Animations/DeltaForceLocomotion/DA_AKLocomotionPresentation')
original=source.get_editor_property('view_fit')
copy=original.copy()
before=bool(original.get_editor_property('enabled'))
copy.set_editor_property('enabled',not before)
hip=copy.get_editor_property('hip')
p=hip.translation
before_x=original.get_editor_property('hip').translation.x
hip.translation=unreal.Vector(12345,67890,-12345)
copy.set_editor_property('hip',hip)
assert original.get_editor_property('enabled')==before
assert original.get_editor_property('hip').translation.x==before_x
bag=unreal.load_asset('/Game/Extraction/Items/DA_Item_Bag_Test')
assert bag.get_editor_property('gear_cells')==64 and bag.get_editor_property('gear_width')==8
sniper=unreal.load_asset('/Game/Extraction/Weapons/BoltSniper/DA_BoltSniper_Presentation')
assert sniper.get_editor_property('scope_overlay').get_path_name()=='/Game/Blueprints/UI/SniperScopeNoBG.SniperScopeNoBG'
assert sniper.get_editor_property('scope_fov')==30.0
fit=sniper.get_editor_property('view_fit')
frames={}
for name in ['hip','aim']:
 t=fit.get_editor_property(name);q=t.rotation
 forward=[1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z),2*(q.x*q.z-q.w*q.y)]
 assert forward[0]>.99,(name,forward)
 frames[name]=dict(forward=forward,position=[t.translation.x,t.translation.y,t.translation.z])
report=dict(result='passed',source_struct_copy_isolated=True,backpack=[8,8],scope_texture='SniperScopeNoBG',scope_fov=30,frames=frames)
(out/'Run.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('KIT_ASSET_RELOAD_PASSED',report)
