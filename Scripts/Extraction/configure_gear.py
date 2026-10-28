"""Author chest rigs, backpacks and safe boxes (胸挂、背包、安全箱).

Sources (local unpack, DataTables / StringTables):
- ``EquipmentPart``: ``GainEquipPosLength x GainEquipPosWidth`` is the item's own
  footprint (DG运动背包 occupies 3x2 yet provides 12 cells), used here as ``size``.
- ``LOC_GameItem``: ``<id>_Name`` and the "提供N格" phrase in ``<id>_Description``
  give the official capacity, used as ``gear_cells``.
- Safe boxes carry no cell count in their description; the only size field is
  2x2 / 3x2 / 4x2, so those are used as both footprint and capacity (inferred).
- ``AuctionProp.GuidePrice`` is recorded where present; the shop uses its own rules.

The official multi-pouch layout is not decoded, so each container is one grid
of ``gear_cells`` cells laid out ``gear_width`` columns wide.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/configure_gear.py"

Idempotent: creates missing assets and rewrites authored fields.
"""
import unreal

FOLDER = '/Game/Extraction/Items'
CUBE = '/Engine/BasicShapes/Cube'

# asset id, official id, label, gear slot, footprint (x, y), cells, width, rarity, description
GEAR = [
    ('Rig_Pouch', 11070001001, '便携胸包', 4, (3, 2), 6, 6, 1, '轻便紧凑，适合存放少量弹药、医疗包。提供 6 格。'),
    ('Rig_Ammo', 11070003001, '简易携行弹挂', 4, (3, 4), 10, 6, 2, '专为携带多个弹匣而设计。提供 10 格。'),
    ('Rig_Assault', 11070004002, '突击者战术背心', 4, (5, 3), 16, 6, 3, '充足的装备挂载空间。提供 16 格。'),
    ('Rig_Hurricane', 11070005001, '飓风战术胸挂', 4, (3, 6), 22, 6, 4, '极高的携行弹药和补给能力。提供 22 格。'),
    ('Bag_Climb', 11080003001, '大型登山包', 5, (3, 4), 16, 6, 2, '超大容量的登山背包。提供 16 格存储空间。'),
    ('Bag_Hike', 11080004002, '野战徒步背包', 5, (5, 3), 24, 6, 3, '优秀的重量分布和背负系统。提供 24 格存储空间。'),
    ('Bag_ALS', 11080005001, 'ALS背负系统', 5, (3, 6), 28, 6, 4, '全新的背负系统。提供 28 格存储空间。'),
    ('Bag_GTO', 11080006004, 'GTO重型战术包', 5, (5, 7), 45, 6, 5, '极高强度和容量的战术背包。提供 45 格存储空间。'),
    ('Safe_Basic', 11090000001, '基础安全箱', 6, (2, 2), 4, 2, 2, '始终确保所有物品的安全和锁定。死亡不掉落。'),
    ('Safe_Advanced', 11090000002, '进阶安全箱', 6, (3, 2), 6, 3, 4, '始终确保所有物品的安全和锁定。死亡不掉落。'),
    ('Safe_Premium', 11090000003, '高级安全箱', 6, (4, 2), 8, 4, 5, '始终确保所有物品的安全和锁定。死亡不掉落。'),
]

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
definition = unreal.load_class(None, '/Script/BrokenEdge.ExtractionItemDefinition')
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class', definition)
mesh = lib.load_asset(CUBE)
assert definition and mesh, 'ExtractionItemDefinition or cube mesh missing'

for ident, official, label, slot, size, cells, width, rarity, description in GEAR:
    assert cells <= 64 and 1 <= width <= 8, ident
    name = 'DA_Item_' + ident
    path = FOLDER + '/' + name
    item = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(
        name, FOLDER, definition, factory)
    assert item, path
    for prop, value in dict(
            item_id=ident,
            label=label,
            kind=unreal.ExtractionItemKind.GEAR,
            size=unreal.IntPoint(*size),
            max_stack=1,
            display_rarity=rarity,
            display_category='Safe' if slot == 6 else ('Backpack' if slot == 5 else 'ChestRig'),
            description='%s（官方 %d）' % (description, official),
            gear_slot=slot,
            gear_cells=cells,
            gear_width=width,
            world_mesh=mesh).items():
        item.set_editor_property(prop, value)
    assert item.get_editor_property('kind') == unreal.ExtractionItemKind.GEAR, ident
    assert item.get_editor_property('gear_cells') == cells, ident
    assert lib.save_loaded_asset(item, only_if_is_dirty=False), path
    print('GEAR_ITEM', ident, slot, cells)

print('EXTRACTION_GEAR_CONFIGURED', len(GEAR))
