"""独立编辑器启动脚本：通过 Enhanced Input 验证单人和双人 PIE。

不调用同步导入；不操作 Rider 中暂停的编辑器。退出前保存完整实测快照。
"""
import unreal, time, json, traceback, os
from pathlib import Path

OUT = Path(unreal.Paths.project_saved_dir())/'Stinger'/('visual_validation.json' if os.environ.get('STINGER_VISUAL_CAPTURE') else 'pie_validation.json')
lib = unreal.StingerAssetLibrary
report = {'sessions':[], 'checks':[]}
state = {'phase':'boot','at':time.monotonic(),'elapsed':0,'players':1,'pawns':[],'pulse':False}

def check(name, passed, detail):
    report['checks'].append({'name':name,'passed':bool(passed),'detail':detail})
    print('STINGER_CHECK',name,passed,detail)

def snapshot(pawn):
    move = pawn.get_editor_property('character_movement')
    native = json.loads(lib.get_validation_state(pawn))
    fp = [m for m in pawn.get_components_by_class(unreal.SkeletalMeshComponent)
          if m.get_editor_property('only_owner_see') and m.get_anim_instance()]
    camera = pawn.get_editor_property('follow_camera')
    return {'name':pawn.get_name(),'local':pawn.is_locally_controlled(),
            'location':str(pawn.get_actor_location()),'speed':pawn.get_velocity().length(),
            'max_speed':move.get_editor_property('max_walk_speed'),
            'crouch_speed':move.get_editor_property('max_walk_speed_crouched'),
            'crouched':pawn.get_editor_property('is_crouched'),'falling':move.is_falling(),
            'sprinting':pawn.is_sprinting(),'first_person':pawn.is_first_person_view(),
            'fov':camera.get_editor_property('field_of_view'),'ammo':native['ammo'] if native['ammo']>=0 else None,
            'combat_state':native['combat_state'],'native':native,
            'fp_instances':[m.get_anim_instance().get_class().get_name() for m in fp],
            'fp_visible':fp[0].is_visible() if fp else False,
            'visual_state':str(fp[0].get_anim_instance().get_editor_property('current_visual_state')) if fp and os.environ.get('STINGER_ISOLATE_INPUT') else None,
            'animation_source':fp[0].get_anim_instance().get_editor_property('current_animation_name') if fp and os.environ.get('STINGER_ISOLATE_INPUT') else None,
            'hand':str(fp[0].get_socket_location('RightHand')) if fp else None,
            'camera':str(camera.get_world_location()),
            'body_owner_hidden':pawn.get_editor_property('mesh').get_editor_property('owner_no_see')}

def inputs(prop,value):
    for pawn in state['pawns']:
        lib.inject_validation_input(pawn.get_controller(),pawn.get_editor_property(prop),unreal.Vector(*value))

def pulse(prop):
    if not state['pulse']:
        inputs(prop,(1,0,0))
        state['pulse'] = True

def advance(phase):
    state.update(phase=phase,at=time.monotonic(),elapsed=0,pulse=False)

def capture(name):
    values = [snapshot(p) for p in state['pawns']]
    report['sessions'][-1]['snapshots'][name] = values
    if os.environ.get('STINGER_VISUAL_CAPTURE') and name in ['equipped','aim','reload','third_person']:
        path=str(OUT.parent / ('view_'+name+'.png'))
        unreal.SystemLibrary.execute_console_command(state['pawns'][0],'HighResShot 1280x720 filename='+path,state['pawns'][0].get_controller())
    return values

def finish():
    report['result'] = 'passed' if report['checks'] and all(c['passed'] for c in report['checks']) and 'error' not in report else 'failed'
    OUT.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(delta):
    try:
        phase = state['phase']
        state['elapsed']+=min(delta,.05)
        elapsed = state['elapsed']
        if time.monotonic()-started > 240:
            raise TimeoutError('PIE validation exceeded 240 seconds: '+phase)
        if phase == 'boot' and elapsed > 3:
            lib.begin_validation_pie(state['players'])
            advance('spawn')
        elif phase == 'spawn':
            worlds = lib.get_validation_worlds()
            pawns=[]
            for world in worlds:
                for pawn in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.BrokenEdgeCharacter):
                    if pawn.is_locally_controlled(): pawns.append(pawn)
            if os.environ.get('STINGER_ISOLATE_INPUT'):
                for pawn in pawns: lib.isolate_validation_input(pawn.get_controller())
            if len(pawns) == state['players'] and elapsed > 8:
                state['pawns']=pawns
                report['sessions'].append({'players':state['players'],'worlds':len(worlds),'snapshots':{}})
                initial=capture('initial')
                check(str(state['players'])+'p_spawn',all(p.get_class().get_name()==os.environ.get('STINGER_CHARACTER_NAME','BP_StingerCharacter_C') for p in pawns),initial)
                check(str(state['players'])+'p_first_person',all(s['first_person'] and s['fp_instances'] for s in initial),initial)
                if os.environ.get('STINGER_ISOLATE_INPUT'):
                    check(str(state['players'])+'p_unarmed_visual',all(not s['fp_visible'] and s['visual_state']=='Unarmed' for s in initial),initial)
                if state['players']>1:
                    remotes=[snapshot(p) for w in worlds for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.BrokenEdgeCharacter) if not p.is_locally_controlled()]
                    report['sessions'][-1]['snapshots']['remote_characters']=remotes
                    check('2p_remote_full_body',len(remotes)==2 and all(not s['first_person'] and not s['fp_instances'] and not s['body_owner_hidden'] for s in remotes),remotes)
                # 将地图中的拾取武器移到玩家附近，仍由真实重叠与装备输入执行拾取。
                server = next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
                server_pawns=unreal.GameplayStatics.get_all_actors_of_class(server,unreal.BrokenEdgeCharacter)
                weapons=unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Weapon)
                for p,w in zip(server_pawns,weapons):
                    w.set_actor_location(p.get_actor_location()+unreal.Vector(20,0,-50),False,True)
                advance('equip')
        elif phase == 'equip':
            if elapsed > 1: pulse('equip_action')
            if elapsed > 3:
                values=capture('equipped')
                check(str(state['players'])+'p_equip',all(s['ammo'] is not None for s in values),values)
                advance('walk')
        elif phase == 'walk':
            inputs('move_action',(0,1,0))
            if elapsed > 1.5:
                values=capture('walk')
                check(str(state['players'])+'p_walk',all(550 < s['speed'] < 650 for s in values),values)
                advance('sprint')
        elif phase == 'sprint':
            inputs('move_action',(0,1,0)); inputs('sprint_action',(1,0,0))
            if elapsed > 1.5:
                values=capture('sprint')
                check(str(state['players'])+'p_sprint',all(s['sprinting'] and 800 < s['speed'] < 900 for s in values),values)
                advance('crouch')
        elif phase == 'crouch':
            pulse('crouch_action'); inputs('move_action',(0,1,0))
            if elapsed > 1.5:
                values=capture('crouch')
                check(str(state['players'])+'p_crouch',all(s['crouched'] and 250 < s['speed'] < 350 for s in values),values)
                advance('uncrouch')
        elif phase == 'uncrouch':
            pulse('crouch_action')
            if elapsed > .5: advance('jump')
        elif phase == 'jump':
            inputs('jump_action',(1,0,0))
            if elapsed > .25:
                values=capture('jump')
                check(str(state['players'])+'p_jump',all(s['falling'] for s in values),values)
                advance('land')
        elif phase == 'land' and elapsed > 1.5: advance('aim')
        elif phase == 'aim':
            inputs('aim_action',(1,0,0))
            inputs('sprint_action',(1,0,0)); inputs('move_action',(0,1,0))
            if elapsed > 1.5:
                values=capture('aim')
                check(str(state['players'])+'p_aim',all(abs(s['fov']-65)<.5 and s['max_speed']==450 and not s['sprinting'] for s in values),values)
                if os.environ.get('STINGER_ISOLATE_INPUT'):
                    check(str(state['players'])+'p_aim_visual_source',all(s['fp_visible'] and s['visual_state']=='Aim' and '/Corrected/MappedV4/' in s['animation_source'] and s['animation_source'].endswith('AR_M_1P_AimIdle_Pose') for s in values),values)
                advance('fire')
        elif phase == 'fire':
            inputs('fire_action',(1,0,0))
            if elapsed > .8:
                values=capture('fire')
                previous=report['sessions'][-1]['snapshots']['equipped']
                check(str(state['players'])+'p_fire',all(s['ammo'] is not None and s['ammo']<p['ammo'] for s,p in zip(values,previous)),values)
                check(str(state['players'])+'p_no_self_damage',all(s['native']['health']==p['native']['health'] for s,p in zip(values,previous)),values)
                advance('reload_wait')
        elif phase == 'reload_wait' and elapsed > .5: advance('reload')
        elif phase == 'reload':
            pulse('reload_action')
            if elapsed > 4:
                values=capture('reload')
                previous=report['sessions'][-1]['snapshots']['fire']
                check(str(state['players'])+'p_reload',all(s['ammo'] is not None and s['ammo']>p['ammo'] for s,p in zip(values,previous)),values)
                advance('third_person')
        elif phase == 'third_person':
            pulse('toggle_camera_view_action')
            if elapsed > .5:
                values=capture('third_person')
                check(str(state['players'])+'p_view_toggle',all(s['local'] and not s['native']['disabled'] and not s['first_person'] and not s['fp_instances'] and not s['body_owner_hidden'] for s in values),values)
                advance('stop')
        elif phase == 'stop':
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
            advance('stopped')
        elif phase == 'stopped' and elapsed > 3:
            if state['players']==1 and not os.environ.get('STINGER_VISUAL_CAPTURE'):
                state['players']=2; state['pawns']=[]; advance('boot')
            else: finish()
    except Exception:
        report['error']=traceback.format_exc()
        unreal.log_error(report['error'])
        finish()

started=time.monotonic()
handle=unreal.register_slate_post_tick_callback(tick)
print('STINGER_VALIDATION_DRIVER_READY')
