"""Create missing inventory Widget Blueprints; existing designer edits are preserved."""
import json
from pathlib import Path
import unreal
result=unreal.InventoryWidgetBuilder.create_inventory_widgets()
print('INVENTORY_WIDGETS',result)
out=Path(unreal.Paths.project_saved_dir())/'InventoryUMG'
out.mkdir(exist_ok=True)
(out/'Assets.json').write_text(json.dumps({'result':result,'assets':list(unreal.EditorAssetLibrary.list_assets('/Game/UI/Looting',recursive=True))},ensure_ascii=False,indent=2),encoding='utf-8')
assert result.startswith('PASSED'),result
