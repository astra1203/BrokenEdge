"""Give imported Nanite drop meshes project-owned, Nanite-ready material parents."""
import hashlib
import json
from pathlib import Path
import unreal

DEST='/Game/Extraction/Items/WorldModels'
SHARED=DEST+'/SharedMaterials'


def repair():
    lib=unreal.EditorAssetLibrary
    parents={}
    rows=[]
    item_clones=[]

    # Older attachment/medical drop meshes can share the same plugin templates.
    # Give their item definitions an owned copy before changing material slots.
    for path in lib.list_assets('/Game/Extraction/Items',False,False):
        definition=unreal.load_asset(path)
        if not isinstance(definition,unreal.ExtractionItemDefinition):continue
        mesh=definition.get_editor_property('world_mesh')
        if not mesh or mesh.get_path_name().startswith(DEST+'/') or not mesh.get_editor_property('nanite_settings').enabled:continue
        if all(s.material_interface and s.material_interface.get_base_material().get_editor_property('used_with_nanite') for s in mesh.get_editor_property('static_materials')):continue
        source=mesh.get_path_name()
        target_path=DEST+'/SharedMeshes/SM_'+hashlib.sha256(source.encode()).hexdigest()[:16]
        target=unreal.load_asset(target_path) if lib.does_asset_exist(target_path) else lib.duplicate_asset(source,target_path)
        assert target,source
        definition.set_editor_property('world_mesh',target)
        assert lib.save_loaded_asset(definition,only_if_is_dirty=False)
        item_clones.append(dict(item=path,source=source,mesh=target.get_path_name()))

    def ready_parent(source):
        source_path=source.get_path_name()
        if source_path in parents:return parents[source_path]
        # Never change Interchange's engine/plugin material templates or source-map assets.
        if source_path.startswith(SHARED+'/'):
            target=source
        else:
            name=('M_' if isinstance(source,unreal.Material) else 'MI_')+hashlib.sha256(source_path.encode()).hexdigest()[:16]
            path=SHARED+'/'+name
            target=unreal.load_asset(path) if lib.does_asset_exist(path) else lib.duplicate_asset(source_path,path)
            assert target,source_path
        parents[source_path]=target
        if isinstance(target,unreal.Material):
            if not target.get_editor_property('used_with_nanite'):
                target.set_editor_property('used_with_nanite',True)
                unreal.MaterialEditingLibrary.recompile_material(target)
        else:
            parent=ready_parent(source.get_editor_property('parent'))
            unreal.MaterialEditingLibrary.set_material_instance_parent(target,parent)
        assert lib.save_loaded_asset(target,only_if_is_dirty=False)
        return target

    paths=lib.list_assets(DEST,True,False)
    for path in paths:
        mesh=unreal.load_asset(path)
        if not isinstance(mesh,unreal.StaticMesh) or not mesh.get_editor_property('nanite_settings').enabled:continue
        changed=False
        for index,slot in enumerate(mesh.get_editor_property('static_materials')):
            material=slot.material_interface
            assert material,(path,index)
            base=material.get_base_material()
            if not base.get_editor_property('used_with_nanite'):
                if isinstance(material,unreal.MaterialInstanceConstant) and material.get_path_name().startswith(DEST+'/'):
                    parent=ready_parent(material.get_editor_property('parent'))
                    unreal.MaterialEditingLibrary.set_material_instance_parent(material,parent)
                    # SetMaterialInstanceParent does not mark the package dirty.
                    assert lib.save_loaded_asset(material,only_if_is_dirty=False)
                else:
                    material=ready_parent(material)
                    mesh.set_material(index,material)
                    changed=True
            assert material.get_base_material().get_editor_property('used_with_nanite'),material.get_path_name()
            rows.append(dict(mesh=path,slot=index,material=material.get_path_name(),base=material.get_base_material().get_path_name(),nanite=True))
        if changed:assert lib.save_loaded_asset(mesh,only_if_is_dirty=False)
    unreal.StingerAssetLibrary.finish_validation_asset_compilation()
    result=dict(material_slots=rows,parents={k:v.get_path_name() for k,v in parents.items()},item_clones=item_clones)
    output=Path(unreal.Paths.project_dir(),'Saved/Extraction/WorldModels/MaterialUsage.json')
    output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print('WORLD_ITEM_MATERIALS_READY',len(rows),len(parents),flush=True)
    return result


if __name__=='__main__':repair()
