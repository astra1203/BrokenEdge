import unreal,json
from pathlib import Path
actor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.EditorAssetLibrary.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47'),unreal.Vector())
mesh=actor.get_editor_property('weapon_mesh')
report={'asset':mesh.get_skeletal_mesh_asset().get_path_name(),'sockets':{str(n):str(mesh.get_socket_transform(n,unreal.RelativeTransformSpace.RTS_COMPONENT)) for n in mesh.get_all_socket_names()},'hand_exists':mesh.does_socket_exist('Hand_R')}
Path(unreal.Paths.project_saved_dir(),'Stinger','weapon_inspection.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('WEAPON_INSPECTION',report)
