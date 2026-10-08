"""Install a full-body traversal slot after the existing Stinger weapon graph."""
import json
import os
from pathlib import Path
import unreal

out=Path(os.environ['TRAVERSAL_EDITOR_OUT'])
asset=unreal.load_asset('/Game/Characters/Stinger/Animations/ThirdPerson/ABP_BrokenEdgeAnimInstance_Stinger')
assert unreal.StingerAssetLibrary.configure_traversal_slot(asset)
assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
(out/'Report.json').write_text(json.dumps(dict(result='passed',asset=asset.get_path_name(),slot='DefaultSlot',
    boundary='Graph configuration only; source-clip alignment and contact validation require rendered runtime evidence.'),indent=2)+'\n',encoding='utf-8')
