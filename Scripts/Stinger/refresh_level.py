import unreal, json
from pathlib import Path
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.load_level('/Game/Characters/Stinger/Maps/L_StingerTest')
report=[]
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    actor.modify()
    root_component=actor.get_editor_property('root_component')
    if root_component: root_component.modify()
    label = actor.get_actor_label()
    if label == 'Stinger_TestFloor': actor.set_actor_scale3d(unreal.Vector(100,100,.5))
    if label == 'Stinger_ThirdPerson_Inspection': actor.set_actor_rotation(unreal.Rotator(pitch=0,yaw=90,roll=0),False)
    if isinstance(actor,unreal.DirectionalLight): actor.set_actor_rotation(unreal.Rotator(pitch=-45,yaw=-35,roll=0),False)
    if label.startswith('Cover_'):
        location=actor.get_actor_location()
        location.y=1000 if location.y>0 else -1000
        actor.set_actor_location(location,False,True)
    if isinstance(actor,unreal.TextRenderActor):
        text=str(actor.text_render.get_editor_property('text'))
        if 'C CROUCH' in text: actor.text_render.set_text('E EQUIP | RMB AIM | LMB FIRE | R RELOAD | CTRL CROUCH | V VIEW')
    report.append({'label':label,'position':str(actor.get_actor_location()),'scale':str(actor.get_actor_scale3d()),'rotation':str(actor.get_actor_rotation())})
assert editor.save_current_level()
Path(unreal.Paths.project_saved_dir(),'Stinger','level_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('STINGER_TEST_MAP_UPDATED')
