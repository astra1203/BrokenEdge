"""Reload saved maps and item definitions; fail on missing visuals, duplicate pane IDs or bad ladder routes."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir())
lib=unreal.EditorAssetLibrary
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report={'items':[],'maps':{}}
for path in lib.list_assets('/Game/Extraction/Items',False,False):
    item=unreal.load_asset(path)
    if not isinstance(item,unreal.ExtractionItemDefinition):continue
    mesh=item.get_editor_property('world_mesh')
    assert mesh or item.get_editor_property('weapon_class'),path
    if mesh:
        assert not mesh.get_path_name().startswith('/Engine/BasicShapes/'),path
        materials=mesh.get_editor_property('static_materials')
        assert materials and all(m.material_interface for m in materials),path
        if mesh.get_editor_property('nanite_settings').enabled:
            assert all(m.material_interface.get_base_material().get_editor_property('used_with_nanite') for m in materials),('Nanite material would fall back',path)
        size=mesh.get_bounding_box().max-mesh.get_bounding_box().min
        scale=item.get_editor_property('world_scale')
        if scale>0:assert 1<max(size.x,size.y,size.z)*scale<250,(path,size,scale)
        report['items'].append(dict(item=path,mesh=mesh.get_path_name(),scale=scale,size=[size.x,size.y,size.z],materials=len(materials)))
    else:report['items'].append(dict(item=path,weapon=str(item.get_editor_property('weapon_class'))))
assert len(report['items'])==367,len(report['items'])
ids=set()
for name,expected in [('SovietMilitary/L_SovietMilitaryExtraction',196),('SpaceCenter/L_SpaceCenterExtraction',1570)]:
    assert levels.load_level('/Game/Maps/'+name)
    unreal.StingerAssetLibrary.finish_validation_asset_compilation()
    panes=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ExtractionBreakableGlass)]
    assert len(panes)==expected,(name,len(panes))
    for pane in panes:
        ident=unreal.GuidLibrary.conv_guid_to_string(pane.get_editor_property('pane_id'))
        assert ident.replace('-','').replace('{','').replace('}','').strip('0'),('Invalid pane ID',name)
        assert ident not in ids,ident
        ids.add(ident)
        component=pane.get_editor_property('pane')
        assert component.static_mesh and component.is_visible(),pane.get_name()
        assert component.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION,pane.get_name()
        assert pane.get_editor_property('break_sound'),pane.get_name()
    ladders=[a for a in actors.get_all_level_actors() if isinstance(a,unreal.ExtractionLadder) and str(a.get_editor_property('source_id')).startswith('Soviet_')]
    if name.startswith('Soviet'):
        assert len(ladders)==3
        assert all(p.get_editor_property('replaces_legacy_window') for p in panes),'Legacy replacements are not owned by native panes'
    report['maps'][name]=dict(panes=len(panes),soviet_ladders=len(ladders))
out=ROOT/'Saved/Extraction/Fix5767AssetAudit';out.mkdir(parents=True,exist_ok=True)
(out/'Report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('FIX5767_ASSET_AUDIT_COMPLETE',len(report['items']),len(ids))
