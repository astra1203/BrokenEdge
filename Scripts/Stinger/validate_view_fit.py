"""Record complete view actions at fixed simulation steps and validate live grip/action state."""
import sys as _presentation_sys
from pathlib import Path as _PresentationPath
_presentation_sys.path.insert(0, str(_PresentationPath(__file__).resolve().parents[1] / "Stinger"))
from character_presentation import get_character_property
import unreal,json,time,traceback
from pathlib import Path
lib=unreal.StingerAssetLibrary
weapon_class=unreal.EditorAssetLibrary.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47')
assert weapon_class is not None, 'Load weapon class before entering PIE'
character_class=unreal.EditorAssetLibrary.load_blueprint_class('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter_Repaired')
defaults=unreal.get_default_object(character_class)
reload_description=json.loads(lib.get_montage_description(defaults.get_editor_property('reload_montage')))
swap_description=json.loads(lib.get_montage_description(defaults.get_editor_property('swap_montage')))
reload_finish=min(n['time'] for n in reload_description['notifies'] if 'ReloadFinished' in n['name'])
swap_finish=min(n['time'] for n in swap_description['notifies'] if 'SwapFinished' in n['name'])
swap_attach=min(n['time'] for n in swap_description['notifies'] if 'AttachWeapons' in n['name'])
out=Path(unreal.Paths.project_saved_dir())/'Stinger'
frames=out/'ViewFitFrames'/time.strftime('%Y%m%d-%H%M%S')
frames.mkdir(parents=True,exist_ok=True)
report={'frame_directory':str(frames),'fps':5,'samples':[],'checks':[],'visual_acceptance':'pending'}
phases=[('idle',.8),('aim',1.2),('fire',.7),('release',.4),('reload',3.2),
        ('fire_again',.7),('release_again',.4),('reload_repeat',3.2),
        ('look_up',.8),('look_down',.8),('crouch',.8),('uncrouch',.4),
        ('jump',1.4),('sprint',1.4),('walk',.7),('equip_secondary',1.),
        ('swap',2.1),('swap_repeat',2.1),('first_person',.8),
        ('hit',.7),('death',.8)]
state={'mode':'boot','warmup':0.,'index':0,'frame':0,'phase_frame':0,'flags':set()}
started=time.monotonic()
def check(name,ok,detail=None):
    report['checks'].append({'name':name,'passed':bool(ok),'detail':detail})
    print('VIEW_FIT_CHECK',name,ok)
def input_action(name,value=(1,0,0)):
    lib.inject_validation_input(pawn.get_controller(),get_character_property(pawn, name),unreal.Vector(*value))
def once(key,fn):
    if key not in state['flags']: fn();state['flags'].add(key)

def place_weapon():
    weapon=lib.spawn_validation_weapon(world,weapon_class,unreal.Vector(9000,9000,300))
    assert weapon is not None
    weapon.set_actor_location(pawn.get_actor_location()+unreal.Vector(20,0,-50),False,True)
    return weapon
def snapshot():
    meshes=pawn.get_components_by_class(unreal.SkeletalMeshComponent)
    body=next((m for m in meshes if isinstance(m.get_anim_instance(),unreal.StingerFirstPersonAnimInstance)),None)
    weapon=next((m for m in meshes if isinstance(m.get_anim_instance(),unreal.StingerFirstPersonWeaponAnimInstance)),None)
    native=json.loads(lib.get_validation_state(pawn))
    body_anim=get_character_property(pawn, 'mesh').get_anim_instance()
    body_montage=body_anim.get_current_active_montage()
    native['body_montage']=body_montage.get_path_name() if body_montage else None
    native['body_montage_position']=body_anim.montage_get_position(body_montage) if body_montage else None
    if body and weapon:
        fit=get_character_property(pawn, 'first_person_weapon_fit')
        ref=fit.get_editor_property('magazine_reference');grip=fit.get_editor_property('magazine_grip')
        mag=weapon.get_socket_transform('b_gun_mag')
        local=ref.rotation.inversed().rotate_vector(grip.translation-ref.translation)
        expected=mag.translation+mag.rotation.rotate_vector(local)
        native['magazine_hand_error']=(expected-body.get_socket_location('LeftHand')).length()
    return {'native':native,'first_person':pawn.is_first_person_view(),
            'body_visible':body.is_visible() if body else False,
            'body_asset':body.get_skeletal_mesh_asset().get_path_name() if body else None,
            'weapon_asset':weapon.get_skeletal_mesh_asset().get_path_name() if weapon else None,
            'state':str(body.get_anim_instance().get_editor_property('current_visual_state')) if body else None,
            'source':body.get_anim_instance().get_editor_property('current_animation_name') if body else None}
def finish(error=None):
    if error: report['error']=error
    report['result']='passed' if not error and report['checks'] and all(c['passed'] for c in report['checks']) else 'failed'
    (out/'view_fit_motion_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    lib.set_validation_fixed_step(0)
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def record(phase,t):
    s=snapshot();s.update(phase=phase,time=t,frame=state['frame'])
    if s['first_person'] and s['body_visible'] and phase not in ['reload','reload_repeat','swap','swap_repeat','hit','death']:
        n=s['native']
        if 'right_grip_error' in n: check(phase+'_right_grip_'+str(state['frame']),n['right_grip_error']<.1,n['right_grip_error'])
        if 'left_grip_error' in n: check(phase+'_left_grip_'+str(state['frame']),n['left_grip_error']<1.,n['left_grip_error'])
    report['samples'].append(s)
    n=s['native']
    if phase in ['reload','swap'] and .2<t<1.2:
        check(phase+'_imported_montage_active_'+str(state['frame']),'view_montage_fraction' in n,n.get('view_montage'))
    if phase in ['reload','reload_repeat'] and s['first_person'] and .2<n.get('view_montage_fraction',-1)<.42:
        check(phase+'_magazine_contact_'+str(state['frame']),n['magazine_hand_error']<.5,n['magazine_hand_error'])
    if 'view_montage_fraction' in n and n.get('body_montage_position') is not None and phase in ['reload','reload_repeat']:
        expected=min(n['body_montage_position']/reload_finish,.999)
        check(phase+'_action_sync_'+str(state['frame']),abs(n['view_montage_fraction']-expected)<.035,[n['view_montage_fraction'],expected])
    if 'view_montage_fraction' in n and n.get('body_montage_position') is not None and phase in ['swap','swap_repeat']:
        fraction=min(n['body_montage_position']/swap_finish,.999);key=swap_attach/swap_finish
        source_key=get_character_property(pawn, 'first_person_weapon_fit').get_editor_property('swap_attach_fraction')
        expected=fraction/key*source_key if fraction<key else source_key+(fraction-key)/(1-key)*(1-source_key)
        check(phase+'_action_sync_'+str(state['frame']),abs(n['view_montage_fraction']-expected)<.05,[n['view_montage_fraction'],expected])
    path=frames/('%05d.png'%len(report['samples']))
    unreal.SystemLibrary.execute_console_command(pawn,'HighResShot 1280x720 filename='+str(path),pawn.get_controller())
def tick(delta):
    global pawn,world,primary_ammo
    try:
        if time.monotonic()-started>600: raise TimeoutError('View fit capture timed out')
        if state['mode']=='boot':
            state['warmup']+=min(delta,.05)
            if state['warmup']>3: lib.begin_validation_pie(1);state.update(mode='spawn',warmup=0)
            return
        if state['mode']=='spawn':
            state['warmup']+=min(delta,.05)
            worlds=lib.get_validation_worlds()
            if state['warmup']<8 or not worlds:return
            world=next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
            pawn=next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.BrokenEdgeCharacter) if p.is_locally_controlled())
            lib.isolate_validation_input(pawn.get_controller())
            assert get_character_property(pawn, 'first_person_mesh_asset') is not None
            assert get_character_property(pawn, 'first_person_anim_class') is not None
            for w in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Weapon):w.set_actor_location(unreal.Vector(8000,8000,100),False,False)
            place_weapon()
            lib.set_validation_fixed_step(1/30)
            state.update(mode='equip',phase_frame=0,flags=set(),last_world_time=unreal.GameplayStatics.get_time_seconds(world));return
        world_time=unreal.GameplayStatics.get_time_seconds(world)
        if world_time<=state['last_world_time']:return
        state['last_world_time']=world_time
        state['frame']+=1;state['phase_frame']+=1;t=state['phase_frame']/30
        if state['mode']=='equip':
            if t>.4: once('equip',lambda:input_action('equip_action'))
            if t>1.2:
                s=snapshot();check('configured_capped_skin',s['body_asset'] is not None and 'CappedView' in s['body_asset'],s)
                check('configured_ak',s['weapon_asset'] is not None and 'SK_KA47_ViewFit' in s['weapon_asset'],s)
                assert s['native']['ammo']>0 and s['body_visible'],'AK view did not equip'
                primary_ammo=s['native']['ammo'];state.update(mode='actions',phase_frame=0,flags=set())
            return
        name,duration=phases[state['index']]
        if name in ['aim','fire','fire_again']: input_action('aim_action')
        if name in ['fire','fire_again']: input_action('fire_action')
        if name in ['reload','reload_repeat']:once('reload',lambda:input_action('reload_action'))
        if name=='look_up':pawn.get_controller().set_control_rotation(unreal.Rotator(pitch=75))
        elif name=='look_down':pawn.get_controller().set_control_rotation(unreal.Rotator(pitch=-75))
        elif name not in ['look_up','look_down']:pawn.get_controller().set_control_rotation(unreal.Rotator())
        if name in ['crouch','uncrouch']:once('crouch',lambda:input_action('crouch_action'))
        if name=='jump':once('jump',lambda:input_action('jump_action'))
        if name in ['sprint','walk']:input_action('move_action',(0,1,0))
        if name=='sprint':input_action('sprint_action')
        if name=='equip_secondary':
            once('spawn_secondary',place_weapon)
            if t>.3:once('equip_secondary',lambda:input_action('equip_action'))
        if name in ['swap','swap_repeat']:once('swap',lambda:input_action('equip_action'))
        if name in ['hit','death']:
            once('damage',lambda:unreal.GameplayStatics.apply_damage(pawn,10 if name=='hit' else 1000,pawn.get_controller(),None,unreal.DamageType))
        if state['frame']%6==0:record(name,t)
        if t>=duration:
            s=snapshot()
            if name in ['reload','reload_repeat']:check(name+'_settled_once',s['native']['ammo']==primary_ammo and s['native']['combat_state']=='ECS_Unoccuiped',s)
            if name in ['reload_repeat','swap_repeat','first_person']:check(name+'_fixed_view',s['first_person'] and s['body_visible'],s)
            if name=='equip_secondary':check('secondary_equipped',s['native']['has_secondary_weapon'],s)
            if name=='death':check('death_cleans_view',not s['first_person'] and s['body_asset'] is None and s['weapon_asset'] is None,s)
            state.update(index=state['index']+1,phase_frame=0,flags=set())
            if state['index']>=len(phases):finish()
    except Exception:
        error=traceback.format_exc();unreal.log_error(error);finish(error)
handle=unreal.register_slate_post_tick_callback(tick)
print('STINGER_VIEW_FIT_MOTION_DRIVER_READY')
