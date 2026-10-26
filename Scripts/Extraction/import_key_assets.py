"""Import the Space Center key card meshes from the source dump and wire them into the cards.

MCP's static mesh import only accepts fbx/obj (FbxFactory), so GLB goes through the
Interchange task here instead. Run with the editor closed:

    UnrealEditor-Cmd.exe BrokenEdge.uproject -unattended -NullRHI -run=pythonscript \
        -script="Scripts/Extraction/import_key_assets.py"

Source models: 可用资源/DeltaForce/Content/Models/Props/Key/props_keycard_Astropark_708 (normal)
and props_keycard_Astropark_Gold_712 (gold), fetched to RawContent/KeyCards by
Scripts/Extraction/fetch_key_assets.py.

Interchange does not flatten the import: each GLB becomes
    <DEST>/<name>/StaticMeshes/<name>
so resolve that exact path instead of guessing.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
RAW = ROOT / 'RawContent/KeyCards'
DEST = '/Game/Extraction/Items/Models/KeyCards'
ITEMS = '/Game/Extraction/Items'

# source stem, card ids that use it
MESHES = [
    ('props_keycard_Astropark_708_UI', ['Key_Monitor', 'Key_Contract', 'Key_EastBridge', 'Key_StaffPassage']),
    ('props_keycard_Astropark_Gold_712_UI', ['Key_President', 'Key_Core']),
]

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
report = {'imported': [], 'assigned': [], 'limits': []}

for stem, cards in MESHES:
    path = f'{DEST}/{stem}/StaticMeshes/{stem}'
    if not lib.does_asset_exist(path):
        task = unreal.AssetImportTask()
        task.filename = str(RAW / f'{stem}.glb')
        task.destination_path = DEST
        task.automated = True
        task.save = True
        task.replace_existing = True
        tools.import_asset_tasks([task])
    mesh = lib.load_asset(path)
    assert isinstance(mesh, unreal.StaticMesh), f'Expected a static mesh at {path}'
    report['imported'].append(mesh.get_path_name())
    for ident in cards:
        card_path = ITEMS + '/DA_Item_' + ident
        card = lib.load_asset(card_path)
        assert card, card_path
        card.set_editor_property('world_mesh', mesh)
        assert lib.save_loaded_asset(card, only_if_is_dirty=False), card_path
        report['assigned'].append({'card': ident, 'mesh': mesh.get_name()})

# Icons stay unset: the source GLB carries no 2D card art, and the inventory falls back
# to the item label. Do not point at unrelated textures to look finished.
report['limits'].append('no 2D icon assigned; card art exists only as a 3D world mesh')
(ROOT / 'Saved/Extraction/KeyAssets.json').parent.mkdir(parents=True, exist_ok=True)
(ROOT / 'Saved/Extraction/KeyAssets.json').write_text(
    json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('EXTRACTION_KEY_ASSETS_IMPORTED', len(report['assigned']))
