"""Author the 8x8 starting-supply pack without changing existing backpack layouts."""
import unreal

lib = unreal.EditorAssetLibrary
path = '/Game/Extraction/Items/DA_Item_Bag_Test'
item = unreal.load_asset(path) if lib.does_asset_exist(path) else lib.duplicate_asset(
    '/Game/Extraction/Items/DA_Item_Bag_GTO', path)
assert item
for name, value in dict(item_id='Bag_Test', label='大型测试背包',
                        description='连续 8×8 主仓，提供 64 格存储空间。',
                        size=unreal.IntPoint(5, 4), gear_slot=5, gear_cells=64, gear_width=8).items():
    item.set_editor_property(name, value)
assert lib.save_loaded_asset(item, only_if_is_dirty=False)
print('TEST_BACKPACK_SAVED', item.get_path_name(), 8, 8)
