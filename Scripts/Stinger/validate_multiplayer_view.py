"""Capture each local PIE viewport facing the other player's complete body."""
import unreal,json,time,traceback
from pathlib import Path
lib=unreal.StingerAssetLibrary
out=Path(unreal.Paths.project_saved_dir())/'Stinger'
state={'phase':'boot','at':time.monotonic(),'index':0}
report={'checks':[],'views':[]};started=time.monotonic()
def advance(phase):state.update(phase=phase,at=time.monotonic())
def finish(error=None):
    if error:report['error']=error
    report['result']='passed' if not error and report['checks'] and all(c['passed'] for c in report['checks']) else 'failed'
    (out/'view_fit_multiplayer_views.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(delta):
    global locals_,worlds
    try:
        if time.monotonic()-started>180:raise TimeoutError('Multiplayer view capture')
        elapsed=time.monotonic()-state['at'];phase=state['phase']
        if phase=='boot' and elapsed>3:lib.begin_validation_pie(2);advance('spawn')
        elif phase=='spawn' and elapsed>8:
            worlds=lib.get_validation_worlds()
            locals_=[p for w in worlds for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BrokenEdgeCharacter) if p.is_locally_controlled()]
            if len(locals_)!=2:return
            for p in locals_:lib.isolate_validation_input(p.get_controller())
            server=next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
            pawns=unreal.GameplayStatics.get_all_actors_of_class(server,unreal.BrokenEdgeCharacter)
            for i,p in enumerate(pawns):p.set_actor_location(unreal.Vector(i*400,0,100),False,True)
            weapons=unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Weapon)
            for p,w in zip(pawns,weapons):w.set_actor_location(p.get_actor_location()+unreal.Vector(20,0,-50),False,True)
            advance('equip')
        elif phase=='equip':
            if elapsed>.6 and not state.get('equipped'):
                for p in locals_:lib.inject_validation_input(p.get_controller(),p.get_editor_property('equip_action'),unreal.Vector(1,0,0))
                state['equipped']=True
            if elapsed>2:
                for p in locals_:p.get_controller().set_control_rotation(unreal.Rotator(yaw=0 if p.get_actor_location().x<200 else 180))
                advance('inspect')
        elif phase=='inspect' and elapsed>1:
            for w in worlds:
                for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BrokenEdgeCharacter):
                    view=[m for m in p.get_components_by_class(unreal.SkeletalMeshComponent) if isinstance(m.get_anim_instance(),unreal.StingerFirstPersonAnimInstance)]
                    local=p.is_locally_controlled()
                    ok=(p.is_first_person_view() and len(view)==1 and view[0].get_editor_property('only_owner_see')) if local else (not p.is_first_person_view() and not view and not p.get_editor_property('mesh').get_editor_property('owner_no_see'))
                    report['checks'].append({'name':w.get_path_name()+'/'+p.get_name(),'passed':bool(ok),'local':local,'native':json.loads(lib.get_validation_state(p))})
            advance('capture')
        elif phase=='capture' and elapsed>.5:
            p=locals_[state['index']];path=out/('view_2p_'+str(state['index'])+'.png')
            unreal.SystemLibrary.execute_console_command(p,'HighResShot 1280x720 filename='+str(path),p.get_controller())
            report['views'].append({'pawn':p.get_path_name(),'position':str(p.get_actor_location()),'rotation':str(p.get_controller().get_control_rotation()),'screenshot':str(path)})
            state['index']+=1;advance('capture' if state['index']<2 else 'done')
        elif phase=='done' and elapsed>1:finish()
    except Exception:finish(traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
