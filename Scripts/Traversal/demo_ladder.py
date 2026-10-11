"""Visible PIE ladder demo using Enhanced Input; only initial placement is teleported.

Run in Unreal Python: import demo_ladder; demo_ladder.start('Run01')
Call stop() to release injected input immediately. Recording is handled separately.
"""
import json
import math
from pathlib import Path
import time
import traceback
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
handle = None
run = None


def xyz(v):
    return [v.x, v.y, v.z]


def stop(reason='stopped'):
    global handle
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    if run:
        run['sub'].inject_input_vector_for_action(run['move'], unreal.Vector(), [], [])
        run['sub'].inject_input_vector_for_action(run['jump'], unreal.Vector(), [], [])
        run['report']['result'] = reason
        save()


def save():
    (run['folder'] / 'Report.json').write_text(json.dumps(run['report'], indent=2), encoding='utf-8')


def capture():
    """One outstanding engine screenshot; never overwrite or invent a rendered frame."""
    r = run
    now = time.monotonic()
    if r.get('pending_frame'):
        path, stamp = r['pending_frame']
        if not path.is_file():
            return
        with (r['folder']/'Frames.csv').open('a', encoding='utf-8') as f:
            f.write(f'{path.name},{stamp:.9f}\n')
        r['pending_frame'] = None
    if now-r.get('capture_time', 0) < .15:
        return
    r['capture_time'] = now
    r['frame_index'] = r.get('frame_index', 0)+1
    path = r['folder']/f"{r['frame_index']:06d}.png"
    r['pending_frame'] = (path, now-r['start'])
    unreal.SystemLibrary.execute_console_command(r['world'],
        f'Shot filename={path.as_posix()} -nosuffix -showui', r['pc'])


def snapshot():
    p, sp = run['pawn'], run['server_pawn']
    return dict(wall_seconds=time.monotonic()-run['start'],
                world_seconds=unreal.GameplayStatics.get_time_seconds(run['world']),
                phase=run['phase'], client=xyz(p.get_actor_location()), server=xyz(sp.get_actor_location()),
                client_mode=str(p.get_component_by_class(unreal.CharacterMovementComponent).movement_mode),
                server_mode=str(sp.get_component_by_class(unreal.CharacterMovementComponent).movement_mode))


def phase(name):
    run['phase'] = name
    run['phase_time'] = unreal.GameplayStatics.get_time_seconds(run['world'])
    run['phase_pos'] = xyz(run['pawn'].get_actor_location())
    run['report']['events'].append(snapshot())
    save()
    unreal.log('LADDER_DEMO_PHASE ' + name)
    unreal.SystemLibrary.execute_console_command(run['world'], 'getall TraversalMovementComponent State')


def face(direction):
    run['pc'].set_control_rotation(unreal.Rotator(pitch=10., yaw=math.degrees(math.atan2(direction[1], direction[0])), roll=0.))


def walk_to(target):
    pos = run['pawn'].get_actor_location()
    dx, dy = target[0]-pos.x, target[1]-pos.y
    dist = math.hypot(dx, dy)
    face([dx, dy])
    return 0. if dist < 10 else min(.55, max(.12, dist/250.))


def tick(delta):
    try:
        r = run
        if r['record']:
            capture()
        if time.monotonic()-r['start'] > 160:
            stop('failed: overall timeout')
            return
        now = unreal.GameplayStatics.get_time_seconds(r['world'])
        elapsed = now-r['phase_time']
        if elapsed > 18:
            stop('failed: phase timeout ' + r['phase'])
            return
        p = r['pawn'].get_actor_location()
        mode = r['pawn'].get_component_by_class(unreal.CharacterMovementComponent).movement_mode
        custom = mode == unreal.MovementMode.MOVE_CUSTOM
        walking = mode == unreal.MovementMode.MOVE_WALKING
        y = 0.
        jump = 0.
        s = r['phase']
        if s == 'settle':
            if elapsed > 2: phase('walk_into_bottom')
        elif s in ('walk_into_bottom', 'reenter_bottom'):
            face(r['facing'])
            y = .4
            if custom: phase('climb_to_pause' if s == 'walk_into_bottom' else 'climb_to_jump')
        elif s == 'climb_to_pause':
            y = .6
            if p.z > r['bottom'][2]+92+170: phase('pause_on_ladder')
        elif s == 'pause_on_ladder':
            if elapsed > .5 and 'pause_z' not in r: r['pause_z'] = p.z
            if elapsed > 2:
                assert custom and abs(p.z-r['pause_z']) < 3, 'Did not hold position on ladder'
                r['report']['pause_drift_cm'] = abs(p.z-r['pause_z'])
                phase('climb_to_top')
        elif s == 'climb_to_top':
            y = .6
            if walking and p.z > r['top'][2]+70: phase('stand_on_roof')
        elif s == 'stand_on_roof':
            if elapsed > 2: phase('clear_top_entry')
        elif s == 'clear_top_entry':
            y = walk_to(r['top_away'])
            if y == 0: phase('return_to_top_entry')
        elif s == 'return_to_top_entry':
            face([-r['facing'][0], -r['facing'][1]])
            y = .35
            if custom: phase('descend')
        elif s == 'descend':
            y = -.6
            if walking and p.z < r['bottom'][2]+115: phase('stand_at_bottom')
        elif s == 'stand_at_bottom':
            if elapsed > 2: phase('clear_bottom_entry')
        elif s == 'clear_bottom_entry':
            y = walk_to(r['bottom_away'])
            if y == 0: phase('reenter_bottom')
        elif s == 'climb_to_jump':
            y = .6
            if p.z > r['bottom'][2]+92+170: phase('jump_off')
        elif s == 'jump_off':
            jump = 1. if elapsed < .3 else 0.
            if mode == unreal.MovementMode.MOVE_FALLING:
                r['report']['jump_falling_seen'] = True
            if elapsed > 1 and walking:
                assert r['report'].get('jump_falling_seen'), 'No falling state after jump'
                phase('return_near_ladder')
        elif s == 'return_near_ladder':
            y = walk_to(r['bottom_away'])
            if y == 0:
                face(r['facing'])
                phase('finished')
        elif s == 'finished':
            if elapsed > 2:
                stop('passed')
                return
        # Transition frames deliberately release movement, avoiding overshooting exits.
        if s != r['phase']: y = jump = 0.
        r['sub'].inject_input_vector_for_action(r['move'], unreal.Vector(0., y, 0.), [], [])
        r['sub'].inject_input_vector_for_action(r['jump'], unreal.Vector(jump, 0., 0.), [], [])
        if now-r.get('sample_time', -999) > .1:
            r['report']['samples'].append(snapshot())
            r['sample_time'] = now
    except Exception:
        stop('failed: ' + traceback.format_exc())


def start(name, record=False):
    global run, handle
    assert handle is None, 'Stop the previous demo first'
    folder = ROOT/'Saved/Extraction/LadderDemo20261011'/name
    folder.mkdir(parents=True, exist_ok=False)
    worlds = unreal.EditorLevelLibrary.get_pie_worlds(True)
    world = next(w for w in worlds if 'UEDPIE_1_' in w.get_path_name())
    server = next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    server_pc = next(c for c in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.PlayerController)
                     if c.player_state.get_player_name() == pc.player_state.get_player_name())
    binding = next(c for c in json.loads((ROOT/'Scripts/Traversal/source_bindings.json').read_text())['candidates']
                   if c['id'] == '9cf77452749b621c')
    assert binding['classification'] == 'usable' and not binding['approach_errors']
    facing = binding['facing']
    bottom, top = binding['points']['bottom_exit'], binding['points']['top_exit']
    run = dict(folder=folder, record=record, world=world, pc=pc, pawn=pc.get_controlled_pawn(),
               server_pawn=server_pc.get_controlled_pawn(), facing=facing, bottom=bottom, top=top,
               bottom_away=[bottom[i]-facing[i]*135 for i in range(3)],
               top_away=[top[i]+facing[i]*230 for i in range(3)],
               sub=next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if s.get_world()==world),
               move=unreal.load_asset('/Game/Input/Actions/IA_Move'),
               jump=unreal.load_asset('/Game/Input/Actions/IA_Jump'), start=time.monotonic(),
               report=dict(ladder=binding['id'], initial_placement_only=True,
                           input='Enhanced Input actions, normal client movement and server prediction', events=[], samples=[]))
    assert run['move'] and run['jump']
    initial = binding['approaches']['bottom']
    run['server_pawn'].set_actor_location(unreal.Vector(initial[0], initial[1], initial[2]+95), False, True)
    face(facing)
    phase('settle')
    handle = unreal.register_slate_post_tick_callback(tick)
    return str(folder)
