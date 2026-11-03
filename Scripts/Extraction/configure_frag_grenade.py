"""Create the frag grenade item and add it to the 7F gear crate recipe.

Official item 21010000001 增强型破片手雷 ("装备延迟引信的破片手雷，威力巨大"), MaxStack 1.
Explosion numbers live in UExtractionRules (Frag*); this only authors the item.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/configure_frag_grenade.py" -NullRHI -unattended -nosound
"""
import unreal

FOLDER = '/Game/Extraction/Items'
GEAR_RECIPE = '/Game/Extraction/Loot/DA_Project7FGearRecipe'
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

definition = unreal.load_class(None, '/Script/BrokenEdge.ExtractionItemDefinition')
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class', definition)
path = FOLDER + '/DA_Item_FragGrenade'
item = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(
    'DA_Item_FragGrenade', FOLDER, definition, factory)
for prop, value in dict(
        item_id='FragGrenade', label='破片手雷', kind=unreal.ExtractionItemKind.THROWABLE,
        size=unreal.IntPoint(1, 1), max_stack=1, display_rarity=2, display_category='Throwable',
        world_mesh=lib.load_asset('/Engine/BasicShapes/Sphere'),
        description='装备延迟引信的破片手雷，威力巨大。G 投掷，Alt+G 低抛。（官方 21010000001）').items():
    item.set_editor_property(prop, value)
assert lib.save_loaded_asset(item, only_if_is_dirty=False)

recipe = unreal.load_asset(GEAR_RECIPE)
stacks = [s for s in recipe.get_editor_property('items') if str(s.get_editor_property('item')) != 'FragGrenade']
for _ in range(2):
    stack = unreal.ExtractionLootStack()
    stack.set_editor_property('item', 'FragGrenade')
    stack.set_editor_property('quantity', 1)
    stacks.append(stack)
recipe.set_editor_property('items', stacks)
assert lib.save_loaded_asset(recipe, only_if_is_dirty=False)
print('FRAG_GRENADE_CONFIGURED', [str(s.get_editor_property('item')) for s in stacks])
