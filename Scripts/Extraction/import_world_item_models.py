"""Import the selected rigid drop models and wire every item to a concrete world visual."""
import gc
import hashlib
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir())
WORK=ROOT/'Saved/Extraction/WorldModels'
selection=json.loads((WORK/'Selection.json').read_text(encoding='utf-8'))
manifest=json.loads((WORK/'ImportManifest.json').read_text(encoding='utf-8'))
DEST='/Game/Extraction/Items/WorldModels'
ITEMS='/Game/Extraction/Items/DA_Item_'
lib=unreal.EditorAssetLibrary
tools=unreal.AssetToolsHelpers.get_asset_tools()
report={'models':{},'items':{},'substitutions':{}}
for index,(source,record) in enumerate(manifest['models'].items()):
    folder=DEST+'/'+record['name']
    path=folder+'/StaticMeshes/'+record['name']
    if not lib.does_asset_exist(path):
        assert hashlib.sha256(Path(record['file']).read_bytes()).hexdigest()==record['sha256']
        task=unreal.AssetImportTask()
        task.filename=record['file'];task.destination_path=DEST
        task.automated=True;task.save=True;task.replace_existing=False
        tools.import_asset_tasks([task])
    meshes=[p for p in lib.list_assets(folder,True,False) if '/StaticMeshes/' in p]
    assert len(meshes)==1,(source,meshes)
    mesh=unreal.load_asset(meshes[0]);assert isinstance(mesh,unreal.StaticMesh)
    report['models'][source]=mesh.get_path_name()
    if index%20==0:
        print('WORLD_MODEL_IMPORTED',index,'/',len(manifest['models']),flush=True)
        gc.collect();unreal.SystemLibrary.collect_garbage()

for ident,record in selection['items'].items():
    definition=unreal.load_asset(ITEMS+ident);assert definition,ident
    mesh=unreal.load_asset(report['models'][record['source']])
    definition.set_editor_property('world_mesh',mesh)
    box=mesh.get_bounding_box();size=box.max-box.min
    largest=max(size.x,size.y,size.z)
    assert largest>0,ident
    scale=1. if definition.get_editor_property('kind') in [unreal.ExtractionItemKind.GEAR,unreal.ExtractionItemKind.ARMOR,unreal.ExtractionItemKind.THROWABLE] else max(12.,min(60.,largest))/largest
    definition.set_editor_property('world_scale',scale)
    assert lib.save_loaded_asset(definition)
    report['items'][ident]=dict(mesh=mesh.get_path_name(),source=record['source'],official=record['official'])

for ident,reference,reason in [
    ('Col_15080090002','Col_15030040003','Source item has no dropped mesh; use the CPU chip model.'),
    ('Electronics','Col_15030040007','Generic electronics represented by a circuit board.'),
    ('Key','Key_Monitor','Generic key represented by the existing key card.')]:
    definition=unreal.load_asset(ITEMS+ident)
    other=unreal.load_asset(ITEMS+reference)
    mesh=other.get_editor_property('world_mesh');assert mesh
    definition.set_editor_property('world_mesh',mesh)
    definition.set_editor_property('world_scale',other.get_editor_property('world_scale'))
    assert lib.save_loaded_asset(definition)
    report['items'][ident]=dict(mesh=mesh.get_path_name())
    report['substitutions'][ident]=dict(reference=reference,reason=reason)

# Safe containers have no official dropped model. A small, textured lockbox
# represents the container; its contents and death-protection rules are unchanged.
safe_source='/Game/Maps/SpaceCenter/Restored/Assets/2d14e9d35010e0c9/SM_M_Commercial_Furniture_SafeBox_06/StaticMeshes/SM_M_Commercial_Furniture_SafeBox_06'
safe_path=DEST+'/SM_SafeContainer'
if not lib.does_asset_exist(safe_path):assert lib.duplicate_asset(safe_source,safe_path)
safe=unreal.load_asset(safe_path)
active=json.loads((ROOT/'Scripts/SpaceCenter/restoration_active.json').read_text(encoding='utf-8'))
profile=json.loads((ROOT/active['profile_file']).read_text(encoding='utf-8'))
native=json.loads((ROOT/profile['work_directory']/'NativeAssets.json').read_text(encoding='utf-8'))
for i,material in enumerate(safe.get_editor_property('static_materials')):
    name=str(material.material_slot_name)
    matches=[v['asset'] for k,v in native.items() if k.startswith('restoration_material:') and k.rsplit('/',1)[-1]==name]
    assert len(matches)==1,(name,matches)
    safe.set_material(i,unreal.load_asset(matches[0]))
assert lib.save_loaded_asset(safe)
size=safe.get_bounding_box().max-safe.get_bounding_box().min
for ident in ['Safe_Basic','Safe_Advanced','Safe_Premium','Safe_Field']:
    definition=unreal.load_asset(ITEMS+ident)
    definition.set_editor_property('world_mesh',safe)
    definition.set_editor_property('world_scale',40./max(size.x,size.y,size.z))
    assert lib.save_loaded_asset(definition)
    report['items'][ident]=dict(mesh=safe.get_path_name())
    report['substitutions'][ident]=dict(reason='Portable scale of a textured lockbox; no official dropped mesh provided.')

report['audit']=[]
for path in lib.list_assets('/Game/Extraction/Items',False,False):
    definition=unreal.load_asset(path)
    if not isinstance(definition,unreal.ExtractionItemDefinition):continue
    ident=str(definition.get_editor_property('item_id'))
    mesh=definition.get_editor_property('world_mesh')
    weapon=definition.get_editor_property('weapon_class')
    assert mesh or weapon,('Missing world visual',ident)
    assert not mesh or not mesh.get_path_name().startswith('/Engine/BasicShapes/'),('Placeholder',ident)
    report['audit'].append(dict(id=ident,mesh=mesh.get_path_name() if mesh else None,weapon=str(weapon),
                              scale=definition.get_editor_property('world_scale')))
(WORK/'ImportReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('WORLD_ITEM_MODELS_COMPLETE',len(report['items']),len(report['audit']))
