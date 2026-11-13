"""Apply a visually reviewed fit to the original Stinger character and test map."""
import sys as _presentation_sys
from pathlib import Path as _PresentationPath
_presentation_sys.path.insert(0, str(_PresentationPath(__file__).resolve().parents[1] / "Stinger"))
from character_presentation import get_character_property, set_character_property, save_character_presentation
import unreal,json
from pathlib import Path
saved=Path(unreal.Paths.project_saved_dir())/'Stinger'
review=json.loads((saved/'view_fit_visual_review.json').read_text(encoding='utf-8'))
assert review['result']=='accepted', 'Review complete action capture before promotion'
motion=json.loads((saved/'view_fit_motion_validation.json').read_text(encoding='utf-8'))
assert motion['result']=='passed' and review['frame_directory']==motion['frame_directory'], 'Review must match the latest successful capture'
lib=unreal.EditorAssetLibrary;root='/Game/Characters/Stinger'
source=unreal.load_asset(root+'/Blueprints/BP_StingerCharacter_Repaired')
target=unreal.load_asset(root+'/Blueprints/BP_StingerCharacter')
assert unreal.BlueprintEditorLibrary.get_blueprint_parent_class(target).get_name()=='BrokenEdgeCharacter'
unreal.BlueprintEditorLibrary.compile_blueprint(source);unreal.BlueprintEditorLibrary.compile_blueprint(target)
a=unreal.get_default_object(source.generated_class());b=unreal.get_default_object(target.generated_class())
properties=['first_person_mesh_asset','first_person_anim_class','first_person_mesh_transform','first_person_aim_mesh_offset',
    'first_person_weapon_fit','first_person_weapon_source_mesh','first_person_weapon_visual_mesh','first_person_weapon_transform',
    'first_person_fire_montage','first_person_reload_montage','first_person_swap_montage']
for name in properties:set_character_property(b, name,get_character_property(a, name))
save_character_presentation(b)
assert lib.save_loaded_asset(target,only_if_is_dirty=False)
gm=unreal.load_asset(root+'/Blueprints/BP_StingerTestGameMode')
unreal.BlueprintEditorLibrary.compile_blueprint(gm)
unreal.get_default_object(gm.generated_class()).set_editor_property('default_pawn_class',target.generated_class())
assert lib.save_loaded_asset(gm,only_if_is_dirty=False)
editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.load_level(root+'/Maps/L_StingerTest')
world=unreal.EditorLevelLibrary.get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode',gm.generated_class())
assert editor.save_current_level()
(saved/'view_fit_promotion.json').write_text(json.dumps({'character':target.get_path_name(),'parent':'BrokenEdgeCharacter','map':root+'/Maps/L_StingerTest','review':review},indent=2),encoding='utf-8')
print('STINGER_VIEW_FIT_PROMOTED')
