"""Exercise real input -> shot -> replicated damage -> death/drop -> respawn in two-player PIE."""
import json
import os
from pathlib import Path
import time
import traceback
import unreal

lib = unreal.StingerAssetLibrary
out = Path(os.environ['STINGER_BASELINE_DIR'])
captures = out / 'Captures'
captures.mkdir(exist_ok=True)
report = {'checks': [], 'snapshots': [], 'input': [], 'rounds': []}
state = {'phase': 'boot', 'at': time.monotonic(), 'round': 0, 'flags': set()}
started = time.monotonic()
weapon_class = unreal.EditorAssetLibrary.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47')
character_default = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class('/Game/Characters/Stinger/Blueprints/BP_StingerCharacter'))
spawn_health = character_default.get_editor_property('health')
report['configured_spawn_health'] = spawn_health

def player_id(pawn):
    return pawn.get_editor_property('player_state').get_editor_property('player_id')

def pawns(world):
    return [p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.BrokenEdgeCharacter)
            if p.get_editor_property('player_state')]

def server_pawn(identity):
    return next(p for p in pawns(server) if player_id(p) == identity)

def local_pawn(identity):
    return next(p for w in lib.get_validation_worlds() for p in pawns(w)
                if p.is_locally_controlled() and player_id(p) == identity)

def equipped(pawn):
    return next((w for w in unreal.GameplayStatics.get_all_actors_of_class(pawn.get_world(), unreal.Weapon)
                 if w.get_owner() == pawn and w.get_editor_property('weapon_state') == unreal.WeaponState.EWS_EQUIPPED), None)

def snapshot(label):
    rows = []
    for wi, world in enumerate(lib.get_validation_worlds()):
        for p in pawns(world):
            native = json.loads(lib.get_validation_state(p))
            weapon = equipped(p)
            rows.append({'world': wi, 'server': world == server, 'id': player_id(p), 'pawn': p.get_name(),
                         'local': p.is_locally_controlled(), 'health': p.get_editor_property('health'),
                         'shield': p.get_editor_property('shield'), 'disabled': native['disabled'],
                         'ammo': native['ammo'], 'weapon': weapon.get_name() if weapon else None,
                         'location': str(p.get_actor_location())})
    report['snapshots'].append({'round': state['round'], 'phase': label, 'pawns': rows})
    return rows

def check(name, ok, detail=None):
    report['checks'].append({'name': str(state['round']) + '_' + name, 'passed': bool(ok), 'detail': detail})
    print('BASELINE_COMBAT_CHECK', name, bool(ok), detail)
    assert ok, name

def advance(phase):
    state.update(phase=phase, at=time.monotonic(), flags=set())

def pulse(pawn, action):
    lib.inject_validation_input(pawn.get_controller(), pawn.get_editor_property(action), unreal.Vector(1, 0, 0))
    if action == 'fire_action':
        report['input'].append({'round': state['round'], 'phase': state['phase'], 'id': player_id(pawn)})

def aim():
    shooter = local_pawn(state['shooter'])
    target = next(p for p in pawns(shooter.get_world()) if player_id(p) == state['victim'])
    body = target.get_editor_property('mesh')
    assert body.does_socket_exist('Spine2')
    center = body.get_socket_location('Spine2')
    camera = shooter.get_editor_property('follow_camera').get_world_location()
    shooter.get_controller().set_control_rotation(unreal.MathLibrary.find_look_at_rotation(camera, center))
    return shooter

def capture(label):
    focus = local_pawn(state['shooter'])
    path = captures / (str(state['round']) + '_' + label + '.png')
    unreal.SystemLibrary.execute_console_command(focus, 'HighResShot 1280x720 filename=' + str(path), focus.get_controller())
    state['last_capture'] = path

def finish(error=None):
    if error: report['error'] = error
    report['result'] = 'passed' if report['checks'] and all(c['passed'] for c in report['checks']) and not error else 'failed'
    (out / 'Combat.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(delta):
    global server, identities
    try:
        elapsed = time.monotonic() - state['at']
        phase = state['phase']
        if time.monotonic() - started > 300: raise TimeoutError('Combat suite timed out in ' + phase)
        if phase == 'boot':
            if elapsed > 3:
                lib.begin_validation_pie(2)
                advance('spawn')
            return
        worlds = lib.get_validation_worlds()
        for w in worlds:
            for p in pawns(w):
                p.get_editor_property('mesh').set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
        if phase == 'spawn':
            if elapsed > 30: raise TimeoutError('Expected two PIE worlds with two players')
            if elapsed < 8 or len(worlds) != 2: return
            server = next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
            if len(pawns(server)) != 2: return
            identities = sorted(player_id(p) for p in pawns(server))
            host = next(player_id(p) for p in pawns(server) if p.is_locally_controlled())
            identities = [next(i for i in identities if i != host), host]
            report['game_mode'] = unreal.GameplayStatics.get_game_mode(server).get_class().get_path_name()
            for w in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.Weapon):
                w.set_actor_location(unreal.Vector(9000, 9000, 100), False, True)
            advance('setup')
        elif phase == 'setup':
            state['shooter'] = identities[state['round']]
            state['victim'] = identities[1 - state['round']]
            for index, identity in enumerate([state['shooter'], state['victim']]):
                p = server_pawn(identity)
                p.set_actor_location(unreal.Vector(2000 + index * 700, 0, 100), False, True)
                p.set_editor_property('can_be_damaged', True)
                p.set_editor_property('shield', 50 if index else 0)
                owner = local_pawn(identity)
                lib.isolate_validation_input(owner.get_controller())
                if not owner.is_first_person_view(): owner.set_first_person_view(True)
                if not equipped(p):
                    # Bind the weapon's BeginPlay overlap handlers before entering the pickup sphere.
                    weapon = lib.spawn_validation_weapon(server, weapon_class, unreal.Vector(9000, 9000, 300))
                    assert weapon
                    weapon.set_actor_location(p.get_actor_location() + unreal.Vector(20, 0, -50), False, True)
            advance('equip')
        elif phase == 'equip':
            if elapsed > .8 and 'equip' not in state['flags']:
                for identity in identities:
                    if not equipped(local_pawn(identity)): pulse(local_pawn(identity), 'equip_action')
                state['flags'].add('equip')
            if elapsed > 2.5:
                rows = snapshot('equipped')
                check('all_peers_equipped', len(rows) == 4 and all(r['weapon'] for r in rows), rows)
                victim = server_pawn(state['victim'])
                state['old_victims'] = {r['world']: r['pawn'] for r in rows if r['id'] == state['victim']}
                state['dropped_weapons'] = {r['world']: r['weapon'] for r in rows if r['id'] == state['victim']}
                state['before'] = rows
                report['rounds'].append({'shooter': state['shooter'], 'victim': state['victim'],
                                         'shooter_is_host': server_pawn(state['shooter']).is_locally_controlled()})
                advance('aim')
        elif phase == 'aim':
            aim()
            if elapsed > 1: advance('single_shot')
        elif phase == 'single_shot':
            shooter = aim()
            if 'fired' not in state['flags']:
                pulse(shooter, 'fire_action')
                state['flags'].add('fired')
            if elapsed > .8:
                rows = snapshot('single_shot')
                before = next(r for r in state['before'] if r['server'] and r['id'] == state['victim'])
                victims = [r for r in rows if r['id'] == state['victim']]
                loss = before['health'] + before['shield'] - victims[0]['health'] - victims[0]['shield']
                check('real_shot_damages_other_player', 0 < loss < before['health'] + before['shield'], loss)
                check('damage_replicated', len(victims) == 2 and len({(r['health'], r['shield']) for r in victims}) == 1, victims)
                for r in rows:
                    if r['id'] == state['shooter']:
                        old = next(b for b in state['before'] if b['world'] == r['world'] and b['id'] == r['id'])
                        check('one_round_consumed_world_' + str(r['world']), r['ammo'] == old['ammo'] - 1, [old['ammo'], r['ammo']])
                        check('no_self_damage_world_' + str(r['world']), r['health'] == old['health'] and r['shield'] == old['shield'])
                capture('damage')
                advance('lethal_fire')
        elif phase == 'lethal_fire':
            victim = server_pawn(state['victim'])
            if victim.get_editor_property('health') <= 0:
                advance('death')
            else:
                pulse(aim(), 'fire_action')
                if elapsed > 8: raise AssertionError('Real fire did not defeat the target')
        elif phase == 'death' and elapsed > .6:
            rows = snapshot('death')
            victims = [r for r in rows if r['id'] == state['victim']]
            check('death_replicated', len(victims) == 2 and all(r['health'] == 0 and r['disabled'] for r in victims), victims)
            dropped = [w for wi, world in enumerate(worlds) for w in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Weapon)
                       if w.get_name() == state['dropped_weapons'][wi]]
            check('death_weapon_dropped_on_all_peers', len(dropped) == 2 and all(w.get_owner() is None and w.get_editor_property('weapon_state') == unreal.WeaponState.EWS_DROPPED for w in dropped), [w.get_name() for w in dropped])
            capture('death')
            advance('respawn')
        elif phase == 'respawn' and elapsed > 5:
            rows = snapshot('respawn')
            victims = [r for r in rows if r['id'] == state['victim']]
            check('respawn_restores_both_peers', len(victims) == 2 and all(r['pawn'] != state['old_victims'][r['world']] and r['health'] == spawn_health and not r['disabled'] for r in victims), victims)
            if state['round'] == 0:
                state['round'] = 1
                advance('setup')
            else:
                advance('finish')
        elif phase == 'finish' and elapsed > .5:
            check('captures_written', len(list(captures.glob('*.png'))) == 4)
            finish()
    except Exception:
        error = traceback.format_exc()
        unreal.log_error(error)
        finish(error)

handle = unreal.register_slate_post_tick_callback(tick)
print('BASELINE_COMBAT_DRIVER_READY')
