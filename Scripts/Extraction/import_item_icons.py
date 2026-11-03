"""Import the icons picked by resolve_item_icons.py and assign them to item definitions.

    UnrealEditor-Cmd.exe BrokenEdge.uproject -run=pythonscript \
        -script="Scripts/Extraction/import_item_icons.py" -NullRHI -unattended -nosound

Textures go to /Game/Extraction/Items/Icons/T_Icon_<item id> as UI textures (no mips,
no streaming) and are written into UExtractionItemDefinition::Icon. Idempotent.
"""
import json
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir())
PNG = ROOT / 'Saved/Extraction/Icons'
FOLDER = '/Game/Extraction/Items/Icons'
plan = json.loads((ROOT / 'Scripts/Extraction/data/item_icons.json').read_text(encoding='utf-8'))['icons']
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

tasks = []
for item in plan:
    task = unreal.AssetImportTask()
    task.filename = str(PNG / (item + '.png'))
    task.destination_path = FOLDER
    task.destination_name = 'T_Icon_' + item
    task.automated = True
    task.replace_existing = True
    task.save = False
    tasks.append(task)
tools.import_asset_tasks(tasks)

assigned = 0
for item in plan:
    texture = unreal.load_asset('%s/T_Icon_%s' % (FOLDER, item))
    assert texture, item
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('never_stream', True)
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    assert lib.save_loaded_asset(texture, only_if_is_dirty=False), item
    definition = unreal.load_asset('/Game/Extraction/Items/DA_Item_' + item)
    if not definition:
        continue
    definition.set_editor_property('icon', texture)
    assert lib.save_loaded_asset(definition, only_if_is_dirty=False), item
    assigned += 1
print('ITEM_ICONS_IMPORTED', len(plan), 'assigned', assigned)
