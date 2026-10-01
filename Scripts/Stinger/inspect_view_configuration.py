import unreal,json
from pathlib import Path
cls=unreal.EditorAssetLibrary.load_blueprint_class('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired')
cdo=unreal.get_default_object(cls)
actor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(cls,unreal.Vector())
def inspect(obj):
    return {p:str(obj.get_editor_property(p)) for p in ['first_person_mesh_asset','first_person_anim_class','first_person_weapon_fit']}
report={'default':inspect(cdo),'constructed':inspect(actor)}
unreal.get_editor_subsystem(unreal.EditorActorSubsystem).destroy_actor(actor)
Path(unreal.Paths.project_saved_dir(),'Stinger','view_configuration_inspection.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('STINGER_CONFIGURATION_INSPECTION',report)
