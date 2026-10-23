"""Update names only. Stable IDs, ammunition, granted instances and presentation assets stay intact."""
import unreal

for item_id, label in [('AK', 'AKM 突击步枪'), ('SCARH', 'SCAR-H 战斗步枪')]:
    asset = unreal.load_asset('/Game/Extraction/Items/DA_Item_' + item_id)
    assert asset and str(asset.get_editor_property('item_id')) == item_id
    asset.set_editor_property('label', label)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log('EXTRACTION_WEAPON_NAMES_PREPARED')
