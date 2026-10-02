"""Exercise real equip/swap/drop flows and inspect world/view meshes every simulation frame."""
import unreal,json,time,traceback,os
from pathlib import Path
lib=unreal.StingerAssetLibrary
players=int(os.environ.get('STINGER_VISIBILITY_PLAYERS','1'))
capture_mode=os.environ.get('STINGER_VISIBILITY_CAPTURE','1')
capture=capture_mode!='0'
weapon_class=unreal.EditorAssetLibrary.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47')
out=Path(os.environ.get('STINGER_BASELINE_DIR', str(Path(unreal.Paths.project_saved_dir())/'Stinger')))
frames=out/'WeaponVisibilityFrames'/(time.strftime('%Y%m%d-%H%M%S')+'-'+str(players)+'p')
frames.mkdir(parents=True,exist_ok=True)
report={'players':players,'fps':30,'frame_directory':str(frames),'samples':[], 'checks':[], 'captures':[]}
phases=[('equip_primary',1.2),('equip_secondary',1.4),('dual_down',.8),('crouch_down',.8),
        ('aim_down',.8),('swap1',1.8),('swap2',1.8),('swap_view',2.),('fire',.7),
        ('reload_view',2.6),('third_person',.8),('first_person',.8),('drop',2.),('re_pickup',1.4),
        ('unpossess',.6),('repossess',1.2),('death',1.),('respawn',5.),('respawn_equip',1.5),('destroy_weapon',.8)]
capture_phases={'dual_down','crouch_down','aim_down','swap1','swap2','swap_view'}
state={'mode':'boot','at':time.monotonic(),'index':0,'step':0,'flags':set(),'capture_index':0}
started=time.monotonic()
locals_=[];server_pawns=[];controllers=[];dropped=[]

def check(name,ok,detail=None):
    report['checks'].append({'name':name,'passed':bool(ok),'detail':detail})
    print('WEAPON_VISIBILITY_CHECK',name,ok)
    if not ok and os.environ.get('STINGER_HOLD_FAILURE'):
        raise AssertionError(name)

def once(key,fn):
    if key not in state['flags']:fn();state['flags'].add(key)

def pulse(prop):
    for p in locals_:
        if p and p.get_controller():lib.inject_validation_input(p.get_controller(),p.get_editor_property(prop),unreal.Vector(1,0,0))

def local_pawns():
    return [p for w in lib.get_validation_worlds() for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BrokenEdgeCharacter) if p.is_locally_controlled()]

def owned_weapons(p):
    return [w for w in unreal.GameplayStatics.get_all_actors_of_class(p.get_world(),unreal.Weapon) if w.get_owner()==p]

def place_weapons():
    for p in server_pawns:
        w=lib.spawn_validation_weapon(server,weapon_class,unreal.Vector(9000,9000,300))
        assert w
        w.set_actor_location(p.get_actor_location()+unreal.Vector(20,0,-50),False,True)

def snapshot(name,t):
    rows=[];views=[];violations=[]
    if name in ['drop','re_pickup']:
        for p,w in zip(server_pawns,dropped):
            sphere=w.get_editor_property('area_sphere')
            native=json.loads(lib.get_validation_state(p))
            report.setdefault('pickup_debug',[]).append({'phase':name,'time':t,'pawn':p.get_name(),'weapon':w.get_name(),
                'overlapping_weapon':native.get('overlapping_weapon'),
                'sphere_overlaps_pawn':sphere.is_overlapping_actor(p),
                'overlapping_components':[c.get_name() for c in sphere.get_overlapping_components()],
                'weapon_position':str(w.get_actor_location()),'sphere_position':str(sphere.get_world_location()),
                'sphere_radius':sphere.get_scaled_sphere_radius(),
                'pawn_position':str(p.get_actor_location()),'native':native})
    for wi,world in enumerate(lib.get_validation_worlds()):
        for w in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Weapon):
            p=w.get_owner();mesh=w.get_editor_property('weapon_mesh');ws=w.get_editor_property('weapon_state')
            fp=isinstance(p,unreal.BrokenEdgeCharacter) and p.is_locally_controlled() and p.is_first_person_view()
            equipped=ws in [unreal.WeaponState.EWS_EQUIPPED,unreal.WeaponState.EWS_EQUIPPED_SECONDARY]
            hidden=mesh.get_editor_property('owner_no_see');primitive=mesh.get_editor_property('first_person_primitive_type')
            expected=bool(fp and equipped)
            ok=hidden==expected and primitive==(unreal.FirstPersonPrimitiveType.WORLD_SPACE_REPRESENTATION if expected else unreal.FirstPersonPrimitiveType.NONE)
            row={'world':wi,'weapon':w.get_name(),'owner':p.get_name() if p else None,'state':str(ws),'hidden':hidden,'primitive':str(primitive),'expected_hidden':expected}
            rows.append(row)
            if not ok:violations.append(row)
        for p in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.BrokenEdgeCharacter):
            view=[m for m in p.get_components_by_class(unreal.SkeletalMeshComponent) if isinstance(m.get_anim_instance(),unreal.StingerFirstPersonWeaponAnimInstance) and m.is_visible()]
            views.append({'pawn':p.get_name(),'world':wi,'local':p.is_locally_controlled(),'first_person':p.is_first_person_view(),'view_mesh_count':len(view)})
            if len(view)>1 or (not p.is_locally_controlled() and view):
                violations.append({'pawn':p.get_name(),'world':wi,'view_mesh_count':len(view)})
    report['samples'].append({'phase':name,'time':t,'world_time':unreal.GameplayStatics.get_time_seconds(server),'weapons':rows,'views':views,'violations':violations})

def record(name,t):
    if not capture or not locals_:return
    focus=locals_[-1] # Client viewport in the two-player run.
    if not focus or not focus.get_controller():return
    if name in capture_phases and capture_mode=='1':
        state['capture_index']+=1
        path=frames/('%05d.png'%state['capture_index'])
        report['captures'].append({'file':path.name,'phase':name,'time':t})
    elif t>=phases[state['index']][1]-1/30 and 'still' not in state['flags']:
        state['flags'].add('still');path=frames/(name+'.png')
        report['captures'].append({'file':path.name,'phase':name,'time':t})
    else:return
    state['last_capture_path']=path
    unreal.SystemLibrary.execute_console_command(focus,'HighResShot 1280x720 filename='+str(path),focus.get_controller())

def finish(error=None):
    if error:report['error']=error
    violations=[s for s in report['samples'] if s['violations']]
    check('every_simulation_frame_visibility',not violations,{'frames':len(report['samples']),'bad_frames':len(violations)})
    intervals=[b['world_time']-a['world_time'] for a,b in zip(report['samples'],report['samples'][1:])]
    check('no_skipped_simulation_frames',bool(intervals) and max(intervals)<1/30+.001,max(intervals) if intervals else None)
    if capture:
        check('capture_requests_written',all((frames/c['file']).is_file() for c in report['captures']) and state.get('last_capture_path',frames).exists())
    report['result']='passed' if not error and all(c['passed'] for c in report['checks']) else 'failed'
    suffix='_stills' if capture_mode=='stills' else ''
    (out/('weapon_visibility_'+str(players)+'p'+suffix+'.json')).write_text(json.dumps(report,indent=2),encoding='utf-8')
    lib.set_validation_fixed_step(0)
    unreal.unregister_slate_post_tick_callback(handle)
    if error and os.environ.get('STINGER_HOLD_FAILURE'):
        unreal.GameplayStatics.set_game_paused(server,True)
        print('VISIBILITY_FAILURE_HELD_FOR_INSPECTION')
        return
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(delta):
    global server,locals_,server_pawns,controllers,dropped
    try:
        if time.monotonic()-started>600:raise TimeoutError('Visibility validation timed out')
        if state['mode']=='drain':
            pending=state.get('last_capture_path')
            if time.monotonic()-state['at']<.5:return
            if pending and not pending.is_file():
                if time.monotonic()-state['at']>15:raise TimeoutError('Final screenshot was not written')
                return
            finish();return
        if state['mode']=='boot':
            if time.monotonic()-state['at']>3:
                lib.begin_validation_pie(players);state.update(mode='spawn',at=time.monotonic())
            return
        if state['mode']=='spawn':
            if time.monotonic()-state['at']<8:return
            worlds=lib.get_validation_worlds()
            if time.monotonic()-state['at']>30:raise TimeoutError('Unexpected PIE topology or missing players')
            if not worlds:return
            server=next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
            server_pawns=unreal.GameplayStatics.get_all_actors_of_class(server,unreal.BrokenEdgeCharacter)
            locals_=local_pawns()
            if len(locals_)!=players or len(server_pawns)!=players:return
            check('requested_pie_topology',len(worlds)==players,{'worlds':len(worlds),'local_players':len(locals_),'server_players':len(server_pawns)})
            controllers=[p.get_controller() for p in server_pawns]
            for p in locals_:lib.isolate_validation_input(p.get_controller())
            for w in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Weapon):w.set_actor_location(unreal.Vector(8000,8000,100),False,True)
            for i,p in enumerate(server_pawns):
                p.set_actor_location(unreal.Vector(2000+i*400,0,100),False,True)
                p.set_editor_property('can_be_damaged',False) # Isolate this scenario from peer fire.
            place_weapons();lib.set_validation_fixed_step(1/30)
            state.update(mode='actions',last_time=unreal.GameplayStatics.get_time_seconds(server));return
        now=unreal.GameplayStatics.get_time_seconds(server)
        if now<=state['last_time']:return # HighResShot can re-enter Slate without advancing the game.
        state['last_time']=now;state['step']+=1;t=state['step']/30
        name,duration=phases[state['index']]
        # Observe the completed game frame before injecting the next action.
        snapshot(name,t)
        if name not in ['unpossess','repossess','death','respawn','respawn_equip']:
            # Fire into the floor so peer damage cannot interrupt this visibility scenario.
            for i,p in enumerate(locals_):p.get_controller().set_control_rotation(unreal.Rotator(pitch=-89 if name in capture_phases else (-60 if name=='fire' else 0),yaw=0 if i==0 else 180))
        if name=='equip_primary':
            if t>.1 and 'overlap_probe' not in state['flags']:
                state['overlap_targets']=[json.loads(lib.get_validation_state(p))['overlapping_weapon'] for p in server_pawns]
                check('initial_pickup_targets_present',all(state['overlap_targets']))
                state['mesh_overlap_flags']=[p.get_editor_property('mesh').generate_overlap_events for p in server_pawns]
                for p in server_pawns:p.get_editor_property('mesh').generate_overlap_events=False
                state['flags'].add('overlap_probe')
            if t>.2 and 'overlap_restore' not in state['flags']:
                retained=[json.loads(lib.get_validation_state(p))['overlapping_weapon'] for p in server_pawns]
                check('capsule_overlap_retains_pickup_target',retained==state['overlap_targets'],retained)
                for p,enabled in zip(server_pawns,state['mesh_overlap_flags']):p.get_editor_property('mesh').generate_overlap_events=enabled
                state['flags'].add('overlap_restore')
            if t>.4:once('equip',lambda:pulse('equip_action'))
        if name=='equip_secondary':
            once('spawn',place_weapons)
            if t>.4:once('equip',lambda:pulse('equip_action'))
        if name=='crouch_down':once('crouch',lambda:pulse('crouch_action'))
        if name in ['aim_down','fire']:pulse('aim_action')
        if name in ['swap1','swap2','swap_view']:once('swap',lambda:pulse('equip_action'))
        if name=='fire':
            once('uncrouch',lambda:pulse('crouch_action'));pulse('fire_action')
        if name=='reload_view':once('reload',lambda:pulse('reload_action'))
        if name in ['swap_view','reload_view']:
            if t>.5:once('view_out',lambda:pulse('toggle_camera_view_action'))
            if t>1.1:once('view_in',lambda:pulse('toggle_camera_view_action'))
        if name in ['third_person','first_person']:once('view',lambda:pulse('toggle_camera_view_action'))
        if name=='drop' and 'drop' not in state['flags']:
            dropped=[next(w for w in owned_weapons(p) if w.get_editor_property('weapon_state')==unreal.WeaponState.EWS_EQUIPPED) for p in server_pawns]
            for p in server_pawns:assert lib.drop_validation_weapon(p)
            state['flags'].add('drop')
        if name=='re_pickup':
            if 'place' not in state['flags']:
                # Approach the dropped weapon on the floor; do not teleport a
                # moving physics body into the character and retain its velocity.
                for p,w in zip(server_pawns,dropped):
                    location=w.get_editor_property('area_sphere').get_world_location()
                    p.set_actor_location(unreal.Vector(location.x-50,location.y-50,p.get_actor_location().z),False,True)
                state['flags'].add('place')
            if t>.4 and 'equip' not in state['flags']:
                check('re_pickup_in_range',all(json.loads(lib.get_validation_state(p))['overlapping_weapon']==w.get_name() for p,w in zip(server_pawns,dropped)))
                pulse('equip_action');state['flags'].add('equip')
        if name=='unpossess' and 'unpossess' not in state['flags']:
            for c in controllers:c.un_possess()
            state['flags'].add('unpossess')
        if name=='repossess':
            if 'possess' not in state['flags']:
                for c,p in zip(controllers,server_pawns):c.possess(p)
                state['flags'].add('possess')
            if t>.5 and 'view' not in state['flags']:
                locals_=local_pawns()
                for p in locals_:lib.isolate_validation_input(p.get_controller())
                pulse('toggle_camera_view_action');state['flags'].add('view')
        if name=='death' and 'damage' not in state['flags']:
            for p in server_pawns:
                p.set_editor_property('can_be_damaged',True)
                unreal.GameplayStatics.apply_damage(p,1000,p.get_controller(),None,unreal.DamageType)
            state['flags'].add('damage')
        if name=='respawn_equip':
            if 'spawn' not in state['flags']:
                server_pawns=[c.get_controlled_pawn() for c in controllers];locals_=local_pawns()
                assert all(server_pawns) and len(locals_)==players
                for p in locals_:lib.isolate_validation_input(p.get_controller())
                for w in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Weapon):w.set_actor_location(unreal.Vector(8000,8000,100),False,True)
                for i,p in enumerate(server_pawns):p.set_actor_location(unreal.Vector(2000+i*400,0,100),False,True)
                place_weapons();state['flags'].add('spawn')
            if t>.5:once('equip',lambda:pulse('equip_action'))
        if name=='destroy_weapon' and 'destroy' not in state['flags']:
            for p in server_pawns:
                for w in owned_weapons(p):w.destroy_actor()
            state['flags'].add('destroy')
        if name not in ['unpossess','death','respawn']:record(name,t)
        if t>=duration:
            if name in ['equip_secondary','swap1','swap2','swap_view','re_pickup']:
                check(name+'_two_owned_weapons',all(len(owned_weapons(p))==2 for p in server_pawns))
            if name=='drop':check('dropped_meshes_restored',all(w.get_owner() is None and not w.get_editor_property('weapon_mesh').get_editor_property('owner_no_see') for w in dropped))
            if name=='unpossess':check('loss_of_control_exits_first_person',all(not p.is_first_person_view() for p in server_pawns))
            if name in ['first_person','repossess','respawn_equip']:check(name+'_local_first_person',len(local_pawns())==players and all(p.is_first_person_view() for p in local_pawns()))
            if name=='death':check('death_restores_weapons',all(w.get_owner() is None for w in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Weapon)))
            if name=='respawn_equip':check('respawn_can_equip',all(len(owned_weapons(p))==1 for p in server_pawns))
            if name in ['dual_down','aim_down','swap1','swap2','swap_view','first_person','re_pickup','respawn_equip']:
                check(name+'_one_local_view_gun',all(v['view_mesh_count']==1 for v in report['samples'][-1]['views'] if v['local']))
            if name in ['third_person','drop','unpossess','death','destroy_weapon']:
                check(name+'_no_view_gun',all(v['view_mesh_count']==0 for v in report['samples'][-1]['views']))
            print('WEAPON_VISIBILITY_PHASE_COMPLETE',name,len(report['samples']))
            state.update(index=state['index']+1,step=0,flags=set())
            if state['index']>=len(phases):state.update(mode='drain',at=time.monotonic())
    except Exception:
        error=traceback.format_exc();unreal.log_error(error);finish(error)

handle=unreal.register_slate_post_tick_callback(tick)
print('STINGER_WEAPON_VISIBILITY_DRIVER_READY',players)
