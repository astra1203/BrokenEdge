"""在新进程重新加载并验证保存后的资产，结果写入 Saved/Stinger。"""
import unreal, json
from pathlib import Path

root = '/Game/Characters/Stinger'
lib = unreal.EditorAssetLibrary
bp = unreal.load_asset(root+'/Blueprints/BP_StingerCharacter')
parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(bp)
assert parent == unreal.BrokenEdgeCharacter.static_class()
cdo = unreal.get_default_object(bp.generated_class())
mesh = cdo.get_editor_property('mesh')
report = {'parent':str(parent), 'mesh':str(mesh.get_skeletal_mesh_asset()),
          'anim_class':str(mesh.get_editor_property('anim_class')), 'properties':{}, 'hitboxes':{}, 'input':[]}
for prop in ['first_person_mesh_asset','first_person_anim_class','enable_sprint',
             'right_hand_bone','first_person_head_bone','sprint_speed','first_person_aim_fov',
             'fire_weapon_montage','reload_montage','swap_montage','default_mapping_context','sprint_action']:
    value = cdo.get_editor_property(prop)
    report['properties'][prop] = str(value)
    assert value is not None, prop
assert cdo.get_editor_property('enable_sprint')
assert '/Stinger/' in mesh.get_skeletal_mesh_asset().get_path_name()
fp_mesh = cdo.get_editor_property('first_person_mesh_asset')
assert '/Meshes/FirstPerson/' in fp_mesh.get_path_name()
assert fp_mesh.get_editor_property('skeleton') == mesh.get_skeletal_mesh_asset().get_editor_property('skeleton')
for box in cdo.get_components_by_class(unreal.BoxComponent):
    report['hitboxes'][box.get_name()] = str(box.get_attach_socket_name())
for mapping in cdo.get_editor_property('default_mapping_context').get_editor_property('default_key_mappings').get_editor_property('mappings'):
    report['input'].append({'action':mapping.get_editor_property('action').get_name(),
                            'key':str(mapping.get_editor_property('key').get_editor_property('key_name'))})
assert any(m['action']=='IA_StingerSprint' and m['key']=='LeftShift' for m in report['input'])
gm = unreal.get_default_object(lib.load_blueprint_class(root+'/Blueprints/BP_StingerTestGameMode'))
assert gm.get_editor_property('default_pawn_class') == bp.generated_class()
assert lib.does_asset_exist(root+'/Maps/L_StingerTest')
report['game_mode_pawn'] = str(gm.get_editor_property('default_pawn_class'))
manifest = json.loads(Path(unreal.Paths.project_saved_dir(),'Stinger','import_manifest.json').read_text(encoding='utf-8'))
assert len(manifest['entries']) == 443
assert all(e['status'] == 'success' for e in manifest['entries'].values())
assert all(lib.does_asset_exist(p) for e in manifest['entries'].values() for p in e['assets'])
report['imported_psa_files'] = len(manifest['entries'])
report['result'] = 'passed'
Path(unreal.Paths.project_saved_dir(),'Stinger','asset_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('STINGER_ASSET_VALIDATION_PASSED')
