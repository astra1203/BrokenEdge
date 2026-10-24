"""Bind presentation assets only; stable item IDs and inventory state remain unchanged."""
import json
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir());source=root/'Saved/Extraction/UISources'
records=json.loads((source/'Manifest.json').read_text(encoding='utf-8'))
lib=unreal.EditorAssetLibrary;tools=unreal.AssetToolsHelpers.get_asset_tools()
for row in records:
    task=unreal.AssetImportTask();task.filename=str(source/row['file']);task.destination_path='/Game/Extraction/UI/Icons'
    task.automated=True;task.save=True;task.replace_existing=True;tools.import_asset_tasks([task])
    texture=unreal.load_asset('/Game/Extraction/UI/Icons/'+Path(row['file']).stem)
    assert texture
    texture.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    assert lib.save_loaded_asset(texture)
    item=unreal.load_asset('/Game/Extraction/Items/DA_Item_'+row['item']);assert item
    item.set_editor_property('icon',texture);assert lib.save_loaded_asset(item)
out=root/'Docs/Extraction/RestorationEvidence/UI';out.mkdir(parents=True,exist_ok=True)
(out/'SourceIcons.json').write_text(json.dumps(records,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('UI_ICONS_CONFIGURED',len(records))
