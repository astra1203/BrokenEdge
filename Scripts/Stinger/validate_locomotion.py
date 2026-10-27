"""Real input, final poses and actual animation sources in one/two-player PIE."""
import json
import math
import os
import time
import traceback
from pathlib import Path
import unreal

lib=unreal.StingerAssetLibrary
weapon_class=unreal.EditorAssetLibrary.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47')
assert weapon_class
floor_mesh=unreal.load_asset('/Engine/BasicShapes/Cube')
players=int(os.environ.get('LOCOMOTION_PLAYERS','1'))
out=Path(os.environ['LOCOMOTION_RUN'])
(out/'Captures').mkdir(parents=True,exist_ok=True)
report={'players':players,'checks':[],'samples':[],'captures':[]}
directions=[('F',0,1),('FR',.707,.707),('R',1,0),('BR',.707,-.707),('B',0,-1),('BL',-.707,-.707),('L',-1,0),('FL',-.707,.707)]
phases=[('equip',0,0,'',2)]
for gait,scale in [('walk',.2),('run',1),('crouch',1)]:
    phases += [(gait+'_'+name,x*scale,y*scale,gait,1.3) for name,x,y in directions]
phases += [('sprint',0,1,'sprint',2),('aim_move',0,1,'aim',1.5),('idle',0,0,'',1),
           ('view_walk',0,.2,'view',1.8),('view_run',0,1,'view',2),('view_sprint',0,1,'view_sprint',2),
           ('view_aim',0,1,'view_aim',1.5),('fire',0,1,'fire',.7),('reload',0,.2,'reload',3.5),
           ('jump',0,1,'jump',.4),('land',0,0,'',1.5)]
state={'mode':'boot','at':time.monotonic(),'index':0,'step':0,'crouched':False,'view':True}
locals=[];server=None;camera=None
started=time.monotonic()

def check(name,ok,detail=None): report['checks'].append(dict(name=name,passed=bool(ok),detail=detail))
def pulse(prop,value=(1,0,0)):
    for p in locals: lib.inject_validation_input(p.get_controller(),p.get_editor_property(prop),unreal.Vector(*value))

def finish(error=None):
    if error: report['error']=error
    for name,x,y,mode,duration in phases:
        if name in ('equip','fire','reload','jump','land'): continue
        samples=[s for s in report['samples'] if s['phase']==name and s['time']>duration-.3]
        rows=[p for s in samples for p in s['pawns']]
        check(name+'_original_body',bool(rows) and all(p['enabled'] and 'DeltaForceLocomotion' in p['body_source'] for p in rows))
        if x or y: check(name+'_moves',bool(rows) and all(p['speed']>10 for p in rows))
        check(name+'_grip',bool(rows) and all(p['left_error']<2.5 and p['right_error']<2.5 for p in rows),
              max((p['left_error'] for p in rows),default=None))
        local=[p for p in rows if p['view_source'] is not None]
        if mode in ('view','view_sprint'):
            expected='Walk' if name=='view_walk' else 'Sprint' if name=='view_sprint' else 'Run'
            check(name+'_original_view',bool(local) and all('DeltaForceLocomotion' in p['view_source'] and p['view_state']==expected for p in local),local)
    check('captures_written',bool(report['captures']) and all(Path(p).exists() for p in report['captures']))
    report['result']='passed' if not error and all(c['passed'] for c in report['checks']) else 'failed'
    (out/'Report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    lib.set_validation_fixed_step(0)
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(delta):
    global locals,server,camera
    try:
        if time.monotonic()-started>600: raise TimeoutError('Locomotion PIE timed out')
        if state['mode']=='drain':
            if time.monotonic()-state['at']>1: finish()
            return
        if state['mode']=='boot':
            if time.monotonic()-state['at']>3:
                lib.begin_validation_pie(players);state.update(mode='spawn',at=time.monotonic())
            return
        if state['mode']=='spawn':
            if time.monotonic()-state['at']<8:return
            worlds=lib.get_validation_worlds()
            if len(worlds)!=players:return
            server=next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
            locals=[p for w in worlds for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BrokenEdgeCharacter) if p.is_locally_controlled()]
            pawns=unreal.GameplayStatics.get_all_actors_of_class(server,unreal.BrokenEdgeCharacter)
            if len(locals)!=players or len(pawns)!=players:return
            # A transient floor extends the test stage; never save or alter the map.
            for world in worlds:
                floor=lib.spawn_validation_weapon(world,unreal.StaticMeshActor,unreal.Vector(0,0,-60))
                comp=floor.get_component_by_class(unreal.StaticMeshComponent)
                comp.set_mobility(unreal.ComponentMobility.MOVABLE)
                comp.set_static_mesh(floor_mesh)
                comp.set_collision_profile_name('BlockAll')
                floor.set_actor_scale3d(unreal.Vector(400,400,1))
            for p in locals:lib.isolate_validation_input(p.get_controller())
            for w in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Weapon):w.set_actor_location(unreal.Vector(8000,8000,100),False,True)
            for i,p in enumerate(pawns):
                p.set_actor_location(unreal.Vector(2000,i*1200,100),False,True)
                p.set_editor_property('can_be_damaged',False)
                gun=lib.spawn_validation_weapon(server,weapon_class,unreal.Vector(9000,9000,300))
                gun.set_actor_location(p.get_actor_location()+unreal.Vector(20,0,-50),False,True)
            camera=lib.spawn_validation_weapon(locals[-1].get_world(),unreal.CameraActor,unreal.Vector())
            camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(65)
            lib.set_validation_fixed_step(1/30)
            state.update(mode='actions',last=unreal.GameplayStatics.get_time_seconds(server))
            return
        now=unreal.GameplayStatics.get_time_seconds(server)
        if now<=state['last']:return
        state['last']=now;state['step']+=1
        t=state['step']/30
        name,x,y,mode,duration=phases[state['index']]
        if state['step']==1:
            crouched=mode=='crouch'
            if crouched!=state['crouched']:pulse('crouch_action');state['crouched']=crouched
            if mode=='reload':pulse('reload_action')
        if name=='equip' and state['step']==20:pulse('equip_action')
        if x or y:pulse('move_action',(x,y,0))
        if 'sprint' in mode:pulse('sprint_action')
        if 'aim' in mode:pulse('aim_action')
        if mode=='fire':pulse('fire_action')
        if mode=='jump':pulse('jump_action')
        for p in locals:p.get_controller().set_control_rotation(unreal.Rotator(yaw=0))
        rows=[]
        for w in lib.get_validation_worlds():
            for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BrokenEdgeCharacter):
                body=p.get_editor_property('mesh')
                body.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
                anim=body.get_anim_instance()
                weapons=[a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Weapon) if a.get_owner()==p and a.get_editor_property('weapon_state')==unreal.WeaponState.EWS_EQUIPPED]
                if not weapons:continue
                gun=weapons[0].get_editor_property('weapon_mesh')
                fp=[c.get_anim_instance() for c in p.get_components_by_class(unreal.SkeletalMeshComponent) if isinstance(c.get_anim_instance(),unreal.StingerFirstPersonAnimInstance)]
                rows.append({'pawn':p.get_path_name(),'local':p.is_locally_controlled(),'speed':p.get_velocity().length(),
                    'enabled':anim.get_editor_property('rifle_locomotion_enabled'),'body_source':anim.get_editor_property('rifle_locomotion_asset'),
                    'coordinates':str(anim.get_editor_property('rifle_locomotion_coordinates')),
                    'left_error':(body.get_socket_location('LeftHand')-gun.get_socket_location('LeftHandSocket')).length(),
                    'right_error':(body.get_socket_location('RightHand')-gun.get_socket_location('Hand_R')).length(),
                    'view_state':str(fp[0].get_editor_property('current_visual_state')) if fp else None,
                    'view_source':fp[0].get_editor_property('current_animation_name') if fp else None,
                    'view_loop':fp[0].get_editor_property('loop_active_animation') if fp else None})
        report['samples'].append(dict(phase=name,time=t,pawns=rows))
        focus=locals[-1];pc=focus.get_controller()
        if state['view']:pc.set_view_target_with_blend(focus,0)
        else:
            target=next((p for p in unreal.GameplayStatics.get_all_actors_of_class(focus.get_world(),unreal.BrokenEdgeCharacter) if not p.is_locally_controlled()),focus)
            center=target.get_actor_location()+unreal.Vector(0,0,10)
            location=center+unreal.Vector(220,-300,100)
            camera.set_actor_location_and_rotation(location,unreal.MathLibrary.find_look_at_rotation(location,center),False,True)
            pc.set_view_target_with_blend(camera,0)
        if state['step']==round(duration*30)-2:
            path=out/'Captures'/(name+'.png');report['captures'].append(str(path))
            unreal.SystemLibrary.execute_console_command(focus,'HighResShot 1280x720 filename='+str(path),pc)
        elif os.environ.get('LOCOMOTION_VIDEO')=='1' and name in ('walk_F','run_R','crouch_F','view_walk','view_run','view_sprint') and state['step']%5==0:
            path=out/'Captures'/('%s_%04d.png'%(name,state['step']));report['captures'].append(str(path))
            unreal.SystemLibrary.execute_console_command(focus,'HighResShot 1280x720 filename='+str(path),pc)
        if t>=duration:
            state.update(index=state['index']+1,step=0)
            if state['index']>=len(phases):state.update(mode='drain',at=time.monotonic())
    except Exception:finish(traceback.format_exc())

handle=unreal.register_slate_post_tick_callback(tick)
