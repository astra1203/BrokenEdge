"""Measure final third-person hand poses and capture actual local/remote AK handling."""
import json
import os
import time
import traceback
from pathlib import Path
import unreal

lib = unreal.StingerAssetLibrary
players = int(os.environ.get('STINGER_GRIP_PLAYERS', '1'))
stage = os.environ.get('STINGER_GRIP_STAGE', 'final')
capture = os.environ.get('STINGER_GRIP_CAPTURE', 'stills')
out = Path(unreal.Paths.project_saved_dir()) / 'Stinger'
frames = out / 'ThirdPersonGripFrames' / (time.strftime('%Y%m%d-%H%M%S') + '-' + stage + '-' + str(players) + 'p')
frames.mkdir(parents=True, exist_ok=True)
report = {'stage': stage, 'players': players, 'fps': 30, 'capture': capture, 'frame_directory': str(frames), 'samples': [], 'captures': [], 'checks': []}
phases = [('equip', 1.4), ('secondary', 1.4), ('idle_left', 1.2), ('idle_front', 1.2),
          ('aim', 1.2), ('look_up', 1.2), ('look_down', 1.2), ('crouch', 1.2),
          ('walk', 1.2), ('strafe', 1.2), ('fire', .7), ('reload', 3.0), ('after_reload', 1.2),
          ('swap', 2.), ('after_swap', 1.2), ('drop', 1.), ('pickup', 1.6), ('after_pickup', 1.2),
          ('missing_socket', .8), ('restored_socket', 1.2),
          ('first_person', 1.2), ('first_person_aim', 1.2), ('third_person', 1.2)]
if stage in ['baseline', 'capture_tail']: phases = phases[:4]
stable = {'idle_left', 'idle_front', 'aim', 'look_up', 'look_down', 'crouch', 'walk', 'strafe',
          'fire', 'after_reload', 'after_swap', 'after_pickup', 'restored_socket', 'third_person'}
video_phases = {'idle_left', 'idle_front', 'aim', 'look_up', 'look_down', 'crouch', 'walk', 'strafe',
                'fire', 'after_reload', 'swap', 'first_person', 'first_person_aim', 'third_person'}
state = {'mode': 'boot', 'at': time.monotonic(), 'index': 0, 'step': 0, 'flags': set(), 'capture_index': 0}
weapon_class = unreal.EditorAssetLibrary.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47')
local_pawns = []
server_pawns = []
dropped = []
missing_meshes = []
started = time.monotonic()

def check(name, ok, detail=None):
    report['checks'].append({'name': name, 'passed': bool(ok), 'detail': detail})
    print('THIRD_PERSON_GRIP_CHECK', name, ok, detail)

def once(key, fn):
    if key not in state['flags']:
        fn()
        state['flags'].add(key)

def pulse(prop, value=unreal.Vector(1, 0, 0)):
    for p in local_pawns: lib.inject_validation_input(p.get_controller(), p.get_editor_property(prop), value)

def owned(p):
    return [w for w in unreal.GameplayStatics.get_all_actors_of_class(p.get_world(), unreal.Weapon) if w.get_owner() == p]

def spawn_weapons():
    for p in server_pawns:
        w = lib.spawn_validation_weapon(server, weapon_class, unreal.Vector(9000, 9000, 300))
        w.set_actor_location(p.get_actor_location() + unreal.Vector(20, 0, -50), False, True)

def snapshot(name, t):
    rows = []
    for wi, world in enumerate(lib.get_validation_worlds()):
        for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.BrokenEdgeCharacter):
            body = p.get_editor_property('mesh')
            native = json.loads(lib.get_validation_state(p))
            row = {'world': wi, 'pawn': p.get_name(), 'local': p.is_locally_controlled(),
                   'aim_pitch': p.get_base_aim_rotation().pitch,
                   'first_person': p.is_first_person_view(), 'combat_state': native.get('combat_state'),
                   'ik_active': native.get('left_hand_ik_active'), 'weapon': None}
            primary = next((w for w in owned(p) if w.get_editor_property('weapon_state') == unreal.WeaponState.EWS_EQUIPPED), None)
            if primary:
                mesh = primary.get_editor_property('weapon_mesh')
                has_left = mesh.does_socket_exist('LeftHandSocket')
                has_right = mesh.does_socket_exist('Hand_R')
                left = body.get_socket_location('LeftHand')
                right = body.get_socket_location('RightHand')
                target = mesh.get_socket_location('LeftHandSocket') if has_left else mesh.get_world_transform().transform_location(unreal.Vector(0, 25, -2))
                rtarget = mesh.get_socket_location('Hand_R') if has_right else mesh.get_world_transform().transform_location(unreal.Vector(0, -8, 0))
                row.update(weapon=primary.get_name(), has_left_socket=has_left, has_right_socket=has_right,
                           left_error=(left - target).length(), right_error=(right - rtarget).length(),
                           target_reach=(target - body.get_socket_location('LeftArm')).length(),
                           left_position=[left.x, left.y, left.z], target_position=[target.x, target.y, target.z],
                           upper_arm_length=(body.get_socket_location('LeftForeArm') - body.get_socket_location('LeftArm')).length(),
                           forearm_length=(left - body.get_socket_location('LeftForeArm')).length(),
                           right_upper_arm_length=(body.get_socket_location('RightForeArm') - body.get_socket_location('RightArm')).length(),
                           right_forearm_length=(right - body.get_socket_location('RightForeArm')).length())
            row['world_weapons'] = [{'state': str(w.get_editor_property('weapon_state')),
                                     'owner_no_see': w.get_editor_property('weapon_mesh').get_editor_property('owner_no_see')} for w in owned(p)]
            rows.append(row)
    report['samples'].append({'phase': name, 'time': t, 'world_time': unreal.GameplayStatics.get_time_seconds(server), 'pawns': rows})

def record(name, t, duration):
    # A capture camera changes Pawn.GetBaseAimRotation through the player's view target.
    # Numerical pitch regressions therefore keep each player's real gameplay camera.
    if capture == '0': return
    focus = local_pawns[-1]
    pc = focus.get_controller()
    if name.startswith('first_person'):
        pc.set_view_target_with_blend(focus, 0)
    else:
        pc.set_view_target_with_blend(camera, 0)
        target = next((p for p in unreal.GameplayStatics.get_all_actors_of_class(focus.get_world(), unreal.BrokenEdgeCharacter)
                       if not p.is_locally_controlled()), focus) if players == 2 else focus
        center = target.get_actor_location() + unreal.Vector(22, 0, 34)
        location = center + (unreal.Vector(165, -75, 12) if name == 'idle_front' else unreal.Vector(25, -180, 8))
        camera.set_actor_location_and_rotation(location, unreal.MathLibrary.find_look_at_rotation(location, center), False, True)
    if capture == 'video' and name in video_phases:
        state['capture_index'] += 1
        path = frames / ('%05d.png' % state['capture_index'])
        report['captures'].append({'file': path.name, 'phase': name, 'time': t})
    elif t >= duration - 1 / 30 and 'still' not in state['flags']:
        state['flags'].add('still')
        path = frames / (name + '.png')
    else: return
    state['last_capture_path'] = path
    unreal.SystemLibrary.execute_console_command(focus, 'HighResShot 1280x720 filename=' + str(path), pc)

def finish(error=None):
    if error: report['error'] = error
    measured = [r for s in report['samples'] if s['phase'] in stable and s['time'] >= .4 for r in s['pawns']]
    check('stable_frames_have_equipped_weapon', bool(measured) and all(r['weapon'] for r in measured))
    equipped = [r for r in measured if r['weapon']]
    check('world_weapon_grip_sockets_exist', bool(equipped) and all(r['has_left_socket'] and r['has_right_socket'] for r in equipped))
    check('stable_left_grip_within_2cm', bool(equipped) and all(r['left_error'] <= 2 for r in equipped), max((r['left_error'] for r in equipped), default=None))
    check('stable_right_grip_within_2cm', bool(equipped) and all(r['right_error'] <= 2 for r in equipped), max((r['right_error'] for r in equipped), default=None))
    if stage not in ['baseline', 'capture_tail']:
        if capture == '0':
            for phase, pitch in [('look_up', 55), ('look_down', -65)]:
                rows = [r for s in report['samples'] if s['phase'] == phase and s['time'] > .4 for r in s['pawns']]
                check(phase + '_actual_aim_pitch', bool(rows) and all(abs((r['aim_pitch'] - pitch + 180) % 360 - 180) < 2 for r in rows))
        check('stable_left_ik_enabled', bool(equipped) and all(r['ik_active'] for r in equipped))
        for phase in ['reload', 'swap', 'drop']:
            rows = [r for s in report['samples'] if s['phase'] == phase and .25 <= s['time'] <= .7 for r in s['pawns']]
            peers = {(r['world'], r['pawn']) for r in rows}
            check(phase + '_releases_left_ik', bool(peers) and all(any(r['ik_active'] is False for r in rows if (r['world'], r['pawn']) == peer) for peer in peers))
        rows = [r for s in report['samples'] if s['phase'] == 'missing_socket' and s['time'] > .3 for r in s['pawns']]
        check('missing_socket_disables_ik', bool(rows) and all(r['ik_active'] is False for r in rows))
        for phase, hidden in [('first_person', True), ('third_person', False)]:
            rows = [r for s in report['samples'] if s['phase'] == phase and s['time'] > .4 for r in s['pawns'] if r['local']]
            check(phase + '_dual_world_weapon_visibility', bool(rows) and all(len(r['world_weapons']) == 2 and all(w['owner_no_see'] == hidden for w in r['world_weapons']) for r in rows))
        for part in ['upper_arm_length', 'forearm_length', 'right_upper_arm_length', 'right_forearm_length']:
            lengths = [r[part] for s in report['samples'] for r in s['pawns'] if r['weapon']]
            check(part + '_not_stretched', bool(lengths) and max(lengths) - min(lengths) < .1, [min(lengths), max(lengths)] if lengths else None)
    intervals = [b['world_time'] - a['world_time'] for a, b in zip(report['samples'], report['samples'][1:])]
    check('no_skipped_simulation_frames', bool(intervals) and max(intervals) < 1 / 30 + .001)
    if capture != '0':
        check('capture_requests_written', all((frames / c['file']).is_file() for c in report['captures']) and state.get('last_capture_path', frames).exists())
    report['result'] = 'passed' if not error and all(c['passed'] for c in report['checks']) else 'failed'
    (out / ('third_person_grip_' + stage + '_' + str(players) + 'p.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
    lib.set_validation_fixed_step(0)
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(delta):
    global server, local_pawns, server_pawns, camera, dropped, missing_meshes
    try:
        if time.monotonic() - started > 900: raise TimeoutError('Third person grip validation timed out')
        if state['mode'] == 'drain':
            # HighResShot writes after this callback. Do not quit on the requesting tick.
            pending = state.get('last_capture_path')
            if time.monotonic() - state['at'] < .5: return
            if pending and not pending.is_file():
                if time.monotonic() - state['at'] > 15: raise TimeoutError('Final screenshot was not written')
                return
            finish()
            return
        if state['mode'] == 'boot':
            if time.monotonic() - state['at'] > 3:
                unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(), 'r.HighResScreenshotDelay 0')
                lib.begin_validation_pie(players)
                state.update(mode='spawn', at=time.monotonic())
            return
        if state['mode'] == 'spawn':
            if time.monotonic() - state['at'] < 8: return
            worlds = lib.get_validation_worlds()
            if not worlds: return
            server = next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
            server_pawns = unreal.GameplayStatics.get_all_actors_of_class(server, unreal.BrokenEdgeCharacter)
            local_pawns = [p for w in worlds for p in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BrokenEdgeCharacter) if p.is_locally_controlled()]
            if len(local_pawns) != players or len(server_pawns) != players: return
            for p in local_pawns: lib.isolate_validation_input(p.get_controller())
            for w in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.Weapon): w.set_actor_location(unreal.Vector(8000, 8000, 100), False, True)
            for i, p in enumerate(server_pawns):
                p.set_actor_location(unreal.Vector(2000, i * 500, 100), False, True)
                p.set_editor_property('can_be_damaged', False)
            spawn_weapons()
            camera = lib.spawn_validation_weapon(local_pawns[-1].get_world(), unreal.CameraActor, unreal.Vector(2000, -180, 140))
            camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(55)
            # A capture-only fill light makes wrist/foregrip contact readable on the shaded side.
            if capture != '0':
                light = lib.spawn_validation_weapon(local_pawns[-1].get_world(), unreal.DirectionalLight, unreal.Vector())
                light.set_actor_rotation(unreal.Rotator(pitch=-35, yaw=65), False)
                light.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(2)
            pulse('toggle_camera_view_action')
            lib.set_validation_fixed_step(1 / 30)
            state.update(mode='actions', last_time=unreal.GameplayStatics.get_time_seconds(server))
            return
        now = unreal.GameplayStatics.get_time_seconds(server)
        if now <= state['last_time']: return
        state['last_time'] = now
        state['step'] += 1
        t = state['step'] / 30
        name, duration = phases[state['index']]
        snapshot(name, t)
        # NullRHI and offscreen peers must evaluate final bones even when no viewport draws them.
        for w in lib.get_validation_worlds():
            for p in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BrokenEdgeCharacter):
                p.get_editor_property('mesh').set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
        for p in local_pawns:
            p.get_controller().set_control_rotation(unreal.Rotator(pitch=55 if name == 'look_up' else (-65 if name == 'look_down' else 0), yaw=0))
        if name == 'equip' and t > .4: once('equip', lambda: pulse('equip_action'))
        if name == 'secondary':
            once('spawn', spawn_weapons)
            if t > .4: once('equip', lambda: pulse('equip_action'))
        if name in ['aim', 'look_up', 'look_down', 'first_person_aim']: pulse('aim_action')
        if name in ['crouch', 'walk']: once('crouch', lambda: pulse('crouch_action'))
        if name in ['walk', 'strafe']: pulse('move_action', unreal.Vector(0, 1, 0) if name == 'walk' else unreal.Vector(1, 0, 0))
        if name == 'fire': pulse('fire_action')
        if name == 'reload': once('reload', lambda: pulse('reload_action'))
        if name == 'swap': once('swap', lambda: pulse('equip_action'))
        if name == 'drop' and 'drop' not in state['flags']:
            dropped = [next(w for w in owned(p) if w.get_editor_property('weapon_state') == unreal.WeaponState.EWS_EQUIPPED) for p in server_pawns]
            for p in server_pawns: assert lib.drop_validation_weapon(p)
            state['flags'].add('drop')
        if name == 'pickup':
            if 'place' not in state['flags']:
                for p, w in zip(server_pawns, dropped): w.set_actor_location(p.get_actor_location() + unreal.Vector(20, 0, -50), False, True)
                state['flags'].add('place')
            if t > .4: once('equip', lambda: pulse('equip_action'))
        if name == 'missing_socket' and 'missing' not in state['flags']:
            for w in lib.get_validation_worlds():
                for p in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BrokenEdgeCharacter):
                    for weapon in owned(p):
                        if weapon.get_editor_property('weapon_state') == unreal.WeaponState.EWS_EQUIPPED:
                            mesh = weapon.get_editor_property('weapon_mesh')
                            missing_meshes.append((mesh, mesh.get_skeletal_mesh_asset()))
                            mesh.set_skeletal_mesh_asset(None)
            state['flags'].add('missing')
        if name == 'restored_socket' and 'restore' not in state['flags']:
            for mesh, asset in missing_meshes: mesh.set_skeletal_mesh_asset(asset)
            state['flags'].add('restore')
        if name in ['first_person', 'third_person']: once('view', lambda: pulse('toggle_camera_view_action'))
        record(name, t, duration)
        if t >= duration:
            state.update(index=state['index'] + 1, step=0, flags=set())
            if state['index'] >= len(phases): state.update(mode='drain', at=time.monotonic())
    except Exception:
        error = traceback.format_exc()
        unreal.log_error(error)
        finish(error)

handle = unreal.register_slate_post_tick_callback(tick)
print('THIRD_PERSON_GRIP_DRIVER_READY', stage, players)
