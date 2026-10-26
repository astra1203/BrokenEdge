"""Author the Space Center key cards (航天基地钥匙卡).

Only this map's key rooms are covered. Each card carries the room id that
``AExtractionKeyRoom::RequiredKey`` compares against, plus how many doors it can
open; the instance stores remaining uses in ``FExtractionItem::Durability``.

Run after the module rebuild that introduced ``KeyRoomId`` / ``KeyUses``:

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/configure_key_cards.py"

The script is idempotent: it creates missing assets and rewrites the authored
fields of existing ones, without touching other items.
"""
import unreal

FOLDER = '/Game/Extraction/Items'
CUBE = '/Engine/BasicShapes/Cube'

# room id, asset id, label, uses, rarity, description
CARDS = [
    ('President', 'Key_President', '总裁室会客室钥匙卡', 2, 4,
     '航天基地 · 总裁室会客室。可开启 2 次，室内有高价值保险柜。'),
    ('Core', 'Key_Core', 'H3核心室钥匙卡', 1, 4,
     '航天基地 · H3核心室。一次使用，室内为顶级物资。'),
    ('Monitor', 'Key_Monitor', 'H1二楼监控室钥匙卡', 2, 3,
     '航天基地 · H1二楼监控室。可开启 2 次。'),
    ('Contract', 'Key_Contract', 'H4合同房钥匙卡', 3, 2,
     '航天基地 · H4合同房。可开启 3 次，入门级物资。'),
    ('EastBridge', 'Key_EastBridge', '东区吊桥卡', 2, 3,
     '航天基地 · 东区吊桥。可开启 2 次。'),
    ('StaffPassage', 'Key_StaffPassage', '员工通道卡', 3, 2,
     '航天基地 · 员工通道。可开启 3 次。'),
]

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
definition = unreal.load_class(None, '/Script/BrokenEdge.ExtractionItemDefinition')
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class', definition)
mesh = lib.load_asset(CUBE)
assert definition and mesh, 'ExtractionItemDefinition or cube mesh missing'

for room, ident, label, uses, rarity, description in CARDS:
    name = 'DA_Item_' + ident
    path = FOLDER + '/' + name
    item = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(
        name, FOLDER, definition, factory)
    assert item, path
    for prop, value in dict(
            item_id=ident,
            label=label,
            kind=unreal.ExtractionItemKind.KEY,
            size=unreal.IntPoint(1, 1),
            max_stack=1,
            max_durability=float(uses),
            display_rarity=rarity,
            display_category='KeyCard',
            description=description,
            key_room_id=room,
            key_uses=uses,
            world_mesh=mesh).items():
        item.set_editor_property(prop, value)
    # ExtractionItems::ValidateItem 要求实例 Durability <= MaxDurability，
    # 而 Make() 对钥匙卡用 KeyUses 初始化，所以两者必须一致。
    assert item.get_editor_property('kind') == unreal.ExtractionItemKind.KEY, ident
    assert item.get_editor_property('key_room_id') == room, ident
    assert item.get_editor_property('key_uses') == uses, ident
    assert int(item.get_editor_property('max_durability')) == uses, ident
    assert lib.save_loaded_asset(item, only_if_is_dirty=False), path
    print('KEY_CARD', room, ident, uses)

print('EXTRACTION_KEY_CARDS_CONFIGURED', len(CARDS))
