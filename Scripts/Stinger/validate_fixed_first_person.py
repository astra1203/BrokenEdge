"""Short single-player PIE check on L_WeaponRange; no persistent gameplay or map edits."""
import json
import time
import traceback
from pathlib import Path
import unreal

lib = unreal.StingerAssetLibrary
out = Path(unreal.Paths.project_saved_dir()) / 'FixedFirstPerson'
out.mkdir(parents=True, exist_ok=True)
report = {'checks': []}
state = {'stage': 'boot', 'at': time.monotonic(), 'started': time.monotonic()}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

def check(name, passed):
    report['checks'].append({'name': name, 'passed': bool(passed)})
    print('FIXED_VIEW_CHECK', name, bool(passed))
    assert passed, name

def advance(stage):
    state.update(stage=stage, at=time.monotonic())

def native(p):
    return json.loads(lib.get_validation_state(p))

def local_meshes(p):
    return [m for m in p.get_components_by_class(unreal.SkeletalMeshComponent)
            if m.get_editor_property('only_owner_see')]

def end(error=None):
    if error:
        report['error'] = error
    report['passed'] = not error and all(c['passed'] for c in report['checks'])
    (out / 'PIE.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(delta):
    try:
        if time.monotonic() - state['started'] > 100:
            raise TimeoutError('Fixed view PIE stage: ' + state['stage'])
        t = time.monotonic() - state['at']
        stage = state['stage']
        if stage == 'boot':
            lib.begin_validation_pie(1)
            advance('spawn')
            return
        worlds = list(lib.get_validation_worlds())
        if not worlds:
            return
        if stage == 'spawn':
            pawns = [p for w in worlds for p in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BrokenEdgeCharacter)]
            local = [p for p in pawns if p.is_locally_controlled() and p.is_player_controlled()]
            if len(local) != 1 or not all(p.is_first_person_view() and native(p)['ammo'] >= 0 for p in local):
                return
            check('player_automatically_first_person', True)
            report['remote_multiplayer_validation'] = 'not_run: training fixture is single-player'
            check('no_camera_boom', all(not p.get_components_by_class(unreal.SpringArmComponent) for p in pawns))
            check('camera_attached_to_capsule', all(p.get_editor_property('follow_camera').get_attach_parent() == p.get_editor_property('capsule_component') for p in local))
            check('legacy_action_removed', not unreal.EditorAssetLibrary.does_asset_exist('/Game/Input/Actions/IA_ToggleCameraView'))
            check('world_body_keeps_animation_updates', all(p.get_editor_property('mesh').get_editor_property('visibility_based_anim_tick_option') == unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES for p in local))
            server = next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
            host = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.BrokenEdgeCharacter) if p.is_locally_controlled())
            state.update(local=local, host=host, controller=host.get_controller(), ammo=native(host)['ammo'])
            for p in local:
                lib.isolate_validation_input(p.get_controller())
            advance('aim')
        elif stage == 'aim':
            for p in state['local']:
                lib.inject_validation_input(p.get_controller(), p.get_editor_property('aim_action'), unreal.Vector(1, 0, 0))
            if t > 1:
                check('aim_keeps_first_person_and_view_gun', all(p.is_first_person_view() and len(local_meshes(p)) >= 2 for p in state['local']))
                advance('fire')
        elif stage == 'fire':
            p = state['host']
            lib.inject_validation_input(p.get_controller(), p.get_editor_property('fire_action'), unreal.Vector(1, 0, 0))
            if t > .4:
                check('fire_spends_ammo', native(p)['ammo'] < state['ammo'])
                advance('reload')
        elif stage == 'reload':
            p = state['host']
            if not state.get('reload_sent'):
                lib.inject_validation_input(p.get_controller(), p.get_editor_property('reload_action'), unreal.Vector(1, 0, 0))
                state['reload_sent'] = True
            if t > 4:
                check('reload_settles_in_first_person', p.is_first_person_view() and native(p)['ammo'] == state['ammo'])
                state['controller'].un_possess()
                advance('unpossess')
        elif stage == 'unpossess' and t > .5:
            p = state['host']
            check('loss_of_control_cleans_local_meshes', not p.is_first_person_view() and not local_meshes(p))
            check('loss_of_control_restores_world_body', not p.get_editor_property('mesh').get_editor_property('owner_no_see'))
            state['controller'].possess(p)
            advance('repossess')
        elif stage == 'repossess' and t > .8:
            p = state['host']
            check('repossess_automatically_reinitializes', p.is_first_person_view() and len(local_meshes(p)) >= 2)
            # Training bypasses raid settlement; exercise only the death presentation RPC.
            report['death_settlement_and_spectating'] = 'not_run: training bypasses raid settlement'
            p.call_method('MulticastDefeated', (False,))
            advance('death')
        elif stage == 'death' and t > .7:
            p = state['host']
            check('death_cleans_local_view', not p.is_first_person_view() and not local_meshes(p))
            check('death_preserves_world_body', p.get_editor_property('mesh') is not None and not p.get_editor_property('mesh').get_editor_property('owner_no_see'))
            end()
    except Exception:
        end(traceback.format_exc())

handle = unreal.register_slate_post_tick_callback(tick)
