"""Give the Space Center key cards a real source.

The active layout (DA_SpaceCenter_Ma1_Loot) carries 12 points: 10 are enabled and
all of them now spawn in every raid (the President and H3 safes are no longer
one-of-N groups, see ``configure_guaranteed_safes.py``). The cards are therefore
assigned explicitly, one per point, so every card has a guaranteed container and
no room holds the card that opens it.

Run with the editor closed:

    UnrealEditor-Cmd.exe BrokenEdge.uproject -unattended -NullRHI -run=pythonscript \
        -script="Scripts/Extraction/configure_key_loot.py"
"""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
AUDIT = ROOT / 'Artifacts/Extraction/RestorationEvidence/Loot/PlacementAudit.json'
FOLDER = '/Game/Extraction/Loot'
LAYOUT = FOLDER + '/DA_SpaceCenter_Ma1_Loot'
BASE_RECIPE = FOLDER + '/DA_ProjectLargeSafeRecipe'

# point id -> card. Each point below is enabled and no two share a compatibility group.
ASSIGN = {
    'd012cec396f31168aa7a': 'Key_President',      # 总裁室外       (President 组)
    'ab5d21758ee9f7c6b8c9': 'Key_Core',           # H3破译站免保区  (H3 组)
    '9fdd0f2bf6da236700eb': 'Key_Monitor',        # H4合同房       (无组)
    '43389cf951e1f4d9c716': 'Key_Contract',       # H2一楼合同房    (无组)
    '77117d6497015537ce4c': 'Key_EastBridge',     # H2一楼井区      (无组)
    'f100904fb3427f45c14c': 'Key_StaffPassage',   # H4一楼车间      (无组)
}

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
recipe_class = unreal.load_class(None, '/Script/BrokenEdge.ExtractionLootRecipe')
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class', recipe_class)


def make_recipe(card):
    name = 'DA_Project' + card.replace('_', '') + 'Recipe'
    path = FOLDER + '/' + name
    recipe = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(
        name, FOLDER, recipe_class, factory)
    assert recipe, path
    stacks = []
    for item, quantity in [(card, 1), ('Electronics', 2), ('Tools', 1)]:
        stack = unreal.ExtractionLootStack()
        stack.set_editor_property('item', item)
        stack.set_editor_property('quantity', quantity)
        stacks.append(stack)
    recipe.set_editor_property('items', stacks)
    assert lib.save_loaded_asset(recipe, only_if_is_dirty=False), path
    return recipe, path


# room / group of every loot point, from the authored audit (point ids match the layout)
rooms, groups = {}, {}
if AUDIT.is_file():
    for point in json.loads(AUDIT.read_text(encoding='utf-8')).get('points', []):
        rooms[point['id']] = point.get('room') or ''
        # the audit stores "None" as a string for ungrouped points
        group = point.get('group') or ''
        groups[point['id']] = '' if group == 'None' else group

own_room = {'Key_President': '总裁室会客室钥匙房', 'Key_Core': 'H3核心室',
            'Key_Monitor': 'H1二楼监控室', 'Key_Contract': 'H4合同房'}
recipes = {card: make_recipe(card) for card in ASSIGN.values()}

layout = lib.load_asset(LAYOUT)
assert layout, LAYOUT
base_recipe = lib.load_asset(BASE_RECIPE)
assert base_recipe, BASE_RECIPE

# get_editor_property returns a copy of the struct array: mutate the element locally
# and write it back by index, otherwise the new recipe never reaches the asset.
points = layout.get_editor_property('points')
assigned = []
for index in range(len(points)):
    point = points[index]
    point_id = str(point.get_editor_property('id'))
    if not point.get_editor_property('enabled'):
        continue
    card = ASSIGN.get(point_id)
    if not card:
        recipe = point.get_editor_property('recipe')
        if recipe and recipe.get_name().startswith('DA_ProjectKey'):
            point.set_editor_property('recipe', base_recipe)
            points[index] = point
        continue
    room = rooms.get(point_id, '')
    assert not (own_room.get(card) and own_room[card] == room), f'{card} would drop in its own room'
    point.set_editor_property('recipe', recipes[card][0])
    points[index] = point
    assigned.append({'point': point_id, 'room': room, 'card': card, 'group': groups.get(point_id) or None})

missing = set(ASSIGN) - {a['point'] for a in assigned}
assert not missing, f'unassigned points: {sorted(missing)}'
seen = {}
for entry in assigned:
    if entry['group']:
        assert entry['group'] not in seen, f"two cards share group {entry['group']}"
        seen[entry['group']] = entry['card']

layout.set_editor_property('points', points)
assert lib.save_loaded_asset(layout, only_if_is_dirty=False), LAYOUT

report = {'layout': LAYOUT, 'assigned': assigned,
          'recipes': {card: path for card, (_, path) in recipes.items()}}
out = ROOT / 'Saved/Extraction/KeyLoot.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('EXTRACTION_KEY_LOOT_CONFIGURED', len(assigned))
