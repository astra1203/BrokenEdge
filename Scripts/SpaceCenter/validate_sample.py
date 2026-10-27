"""Rendered sample capture and input-driven traversal in a disposable editor."""
import json
import math
import os
import sys
import shutil
from pathlib import Path
import time
import traceback
import unreal

OUT = Path(os.environ['SPACECENTER_RUN_DIR'])
SUITE = os.environ['SPACECENTER_SUITE']
PLAYERS = int(os.environ.get('SPACECENTER_PLAYERS', '1'))
RESTORATION = os.environ.get('SPACECENTER_RESTORATION_VALIDATION') == '1'
HEADLESS = os.environ.get('SPACECENTER_HEADLESS') == '1'
LIB = unreal.StingerAssetLibrary
report = {'checks': [], 'captures': [], 'samples': [], 'suite': SUITE, 'players': PLAYERS}
report['render_mode'] = 'headless_input_driven_pie' if HEADLESS else 'rendered_pie'
state = {'phase': 'boot', 'elapsed': 0, 'index': 0, 'phase_start': time.monotonic()}
started = time.monotonic()
shots = [
    ('overview', [9500, 8500, 9500], [800, -600, 700]),
    ('top', [500, -700, 16000], [500, -700, 0]),
    ('interior', [-1700, -500, 550], [800, -500, 550]),
    ('exterior', [-4500, 5500, 1000], [0, -500, 600]),
]


def check(name, passed, detail=None):
    report['checks'].append({'name': name, 'passed': bool(passed), 'detail': detail})


def vector(v):
    return [v.x, v.y, v.z]


def advance(phase):
    state.update(phase=phase, elapsed=0, phase_start=time.monotonic(), pulse=False)


def inject(pawn, action, value=(1, 0, 0)):
    LIB.inject_validation_input(pawn.get_controller(), pawn.get_editor_property(action), unreal.Vector(*value))


def pulse_all(action):
    if not state['pulse']:
        for p in state['pawns']:
            inject(p, action)
        state['pulse'] = True


def capture(name, pawn):
    if HEADLESS:return
    name += '.png'
    unreal.SystemLibrary.execute_console_command(pawn, 'HighResShot '+('1920x1080' if RESTORATION else '1280x720')+' filename=' + str(OUT / name), pawn.get_controller())
    report['captures'].append(name)


def snapshot(p):
    return {'location': vector(p.get_actor_location()), 'speed': p.get_velocity().length(),
            'player_id': player_id(p), 'world': p.get_world().get_path_name(), 'name': p.get_name(),
            'falling': p.character_movement.is_falling(), 'crouched': p.get_editor_property('is_crouched'),
            'first_person': p.is_first_person_view(), 'native': json.loads(LIB.get_validation_state(p))}


def player_id(p):
    ps = p.get_editor_property('player_state')
    return ps.get_editor_property('player_id') if ps else None


def walk(pawn, target, delta):
    location = pawn.get_actor_location()
    dx, dy = target[0] - location.x, target[1] - location.y
    distance = math.hypot(dx, dy)
    tolerance=state.get('route',{}).get('point_tolerance_cm',28)
    if distance < tolerance and abs(location.z - target[2] - 94) < 70:
        state['stalled'] = 0
        return True
    state['stalled'] = state.get('stalled', 0) + delta
    if state['stalled'] > 8:
        raise RuntimeError('Walk stalled: ' + json.dumps({'target': target, 'state': snapshot(pawn), 'segment': state['segment'], 'point': state['point']}))
    pawn.get_controller().set_control_rotation(unreal.Rotator(yaw=math.degrees(math.atan2(dy, dx))))
    inject(pawn, 'move_action', (0, min(1, max(.2, distance / 120)), 0))
    return False


def finish(error=None):
    if error:
        report['error'] = error
    if state.get('profiling'):
        unreal.SystemLibrary.execute_console_command(state['pawns'][0], 'csvprofile STOP', state['pawns'][0].get_controller())
    report['result'] = 'passed' if report['checks'] and all(c['passed'] for c in report['checks']) and not error else 'failed'
    report['elapsed_seconds'] = time.monotonic() - started
    (OUT / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(delta):
    try:
        state['elapsed'] += delta
        phase = state['phase']
        elapsed = state['elapsed']
        if time.monotonic() - started > int(os.environ.get('SPACECENTER_VALIDATION_TIMEOUT','1200')):
            raise TimeoutError('Sample validation timed out: ' + phase)
        if phase == 'boot' and elapsed > 5:
            if os.environ.get('SPACECENTER_RESTORE_TASK')=='traverse-integrated':
                # Disposable inspection settings; run_restoration_editor verifies
                # that neither production map changes on disk.
                world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
                world.get_world_settings().set_editor_property('default_game_mode',unreal.EditorAssetLibrary.load_blueprint_class('/Game/Characters/Stinger/Blueprints/BP_StingerTestGameMode'))
                stream=unreal.GameplayStatics.get_streaming_level(world,'/Game/Maps/SpaceCenter/L_SpaceCenterNorth')
                assert stream and stream.get_loaded_level()
                unreal.EditorLevelUtils.set_level_visibility(stream.get_loaded_level(),True,False)
                stream.set_editor_property('initially_loaded',True)
                stream.set_editor_property('initially_visible',True)
            state['weapon_class'] = unreal.EditorAssetLibrary.load_blueprint_class('/Game/Blueprints/Weapon/BP_AK47')
            if RESTORATION:
                unreal.SystemLibrary.execute_console_command(None,'Editor.AsyncAssetCompilationFinishAll')
                unreal.SystemLibrary.collect_garbage()
            LIB.begin_validation_pie(PLAYERS)
            advance('spawn')
        elif phase == 'spawn':
            worlds = LIB.get_validation_worlds()
            pawns = [p for w in worlds for p in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BrokenEdgeCharacter) if p.is_locally_controlled()]
            if len(pawns) != PLAYERS or elapsed < 5:
                return
            pawns.sort(key=lambda p: p.get_actor_location().y)
            state['pawns'] = pawns
            for p in pawns:
                LIB.isolate_validation_input(p.get_controller())
                if RESTORATION and not HEADLESS:assert LIB.set_validation_viewport_size(p.get_controller(),1920,1080)
            check('stinger_spawn', all(p.get_class().get_name() == 'BP_StingerCharacter_C' for p in pawns), [vector(p.get_actor_location()) for p in pawns])
            if SUITE == 'traverse':
                for command in ['r.VSync 0', 't.MaxFPS 0', 't.IdleWhenNotForeground 0', 'r.ScreenPercentage 100',
                                'sg.ViewDistanceQuality 2', 'sg.AntiAliasingQuality 2', 'sg.ShadowQuality 2',
                                'sg.GlobalIlluminationQuality 2', 'sg.ReflectionQuality 2', 'sg.PostProcessQuality 2',
                                'sg.TextureQuality 2', 'sg.EffectsQuality 2', 'sg.FoliageQuality 2', 'sg.ShadingQuality 2']:
                    if RESTORATION and command.startswith('sg.'):command=command.rsplit(' ',1)[0]+' 1'
                    unreal.SystemLibrary.execute_console_command(pawns[0], command, pawns[0].get_controller())
                route_file=Path(os.environ.get('SPACECENTER_TRAVERSE_ROUTE',str(Path(unreal.Paths.project_dir())/'Scripts/SpaceCenter/route.json')))
                state['route'] = json.loads(route_file.read_text(encoding='utf-8'))
                report['route'] = state['route']
                if RESTORATION:
                    sys.path.insert(0,str(Path(unreal.Paths.project_dir())/'Scripts/SpaceCenter'))
                    from restoration_terrain_audit import audit
                    terrain=audit(pawns[0].get_world(),json.loads(Path(os.environ['SPACECENTER_RESTORE_MANIFEST']).read_text(encoding='utf-8')))
                    (OUT/'TerrainCollision.json').write_text(json.dumps(terrain,indent=2)+'\n',encoding='utf-8')
                    check('all_original_terrain_collision',terrain['result']=='passed',
                          {k:v for k,v in terrain.items() if k!='probes'})
                    if terrain['result']!='passed':raise RuntimeError('Native terrain collision audit failed')
                server = next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
                if not unreal.GameplayStatics.get_all_actors_of_class(server, unreal.Weapon):
                    for p in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.BrokenEdgeCharacter):
                        LIB.spawn_validation_weapon(server, state['weapon_class'], p.get_actor_location() + unreal.Vector(20, 0, -50))
                if RESTORATION:
                    # Spawn outside the overlap sphere, then enter it through
                    # real movement. The old sample already has world weapons
                    # before its pawn spawns; the isolated preview has none.
                    for weapon in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Weapon):
                        weapon.set_actor_location(weapon.get_actor_location()+unreal.Vector(400,0,0),False,True)
                    state['pickup_weapons']=list(unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Weapon))
                advance('pickup_approach' if RESTORATION else 'equip')
                if RESTORATION:unreal.SystemLibrary.collect_garbage()
                return
            probes = []
            world = pawns[0].get_world()
            for x in range(-5500, 7501, 100):
                for y in range(-7000, 7001, 100):
                    seen_heights = set()
                    for height in [450, 900, 1500]:
                        hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, height), unreal.Vector(x, y, -300),
                                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, pawns, unreal.DrawDebugTrace.NONE)
                        if not hit:
                            continue
                        values = hit.to_tuple()
                        point, normal = values[5], values[7]
                        floor_key = round(point.z / 5)
                        if normal.z > .65 and floor_key not in seen_heights:
                            seen_heights.add(floor_key)
                            clearance = unreal.SystemLibrary.capsule_trace_single(world,
                                point + unreal.Vector(0, 0, 135), point + unreal.Vector(0, 0, 136),
                                42., 96., unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, pawns, unreal.DrawDebugTrace.NONE)
                            probes.append({'location': vector(point), 'normal': vector(normal), 'clear': clearance is None,
                                           'actor': values[9].get_actor_label() if values[9] else None})
            report['probes'] = probes
            (OUT / 'Probes.json').write_text(json.dumps(probes), encoding='utf-8')
            sys.path.insert(0, str(Path(unreal.Paths.project_dir()) / 'Scripts/SpaceCenter'))
            from plan_route import FloorGraph
            graph = FloorGraph(probes)
            blocked, tested = [], set()
            for key in graph.nodes:
                for other in graph.adjacent(key):
                    edge = frozenset((key, other))
                    a, b = graph.nodes[key]['location'], graph.nodes[other]['location']
                    if edge in tested or abs(a[2] - b[2]) > 20:
                        continue
                    tested.add(edge)
                    va, vb = unreal.Vector(*a) + unreal.Vector(0, 0, 135), unreal.Vector(*b) + unreal.Vector(0, 0, 135)
                    hit = unreal.SystemLibrary.capsule_trace_single(world, va, vb, 44., 94., unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, pawns, unreal.DrawDebugTrace.NONE)
                    if not hit:
                        hit = unreal.SystemLibrary.capsule_trace_single(world, vb, va, 44., 94., unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, pawns, unreal.DrawDebugTrace.NONE)
                    if hit:
                        values = hit.to_tuple()
                        if values[5].z - min(a[2], b[2]) < 45:
                            continue  # Walkable step contact; actual character traversal still required.
                        blocked.append({'from': key, 'to': other, 'actor': values[9].get_actor_label() if values[9] else None})
            (OUT / 'BlockedEdges.json').write_text(json.dumps(blocked), encoding='utf-8')
            report['probe_edges'] = {'tested': len(tested), 'blocked': len(blocked)}
            print('SPACE_PROBES', len(probes))
            state['camera'] = LIB.spawn_validation_weapon(pawns[0].get_world(), unreal.CameraActor, unreal.Vector(0, 0, 5000))
            if SUITE == 'capture':
                pawns[0].get_controller().set_view_target_with_blend(state['camera'], 0)
                advance('camera')
        elif phase == 'pickup_approach':
            p=state['pawns'][0]
            weapon=min(state['pickup_weapons'],key=lambda w:math.dist(vector(p.get_actor_location())[:2],vector(w.get_actor_location())[:2]))
            separation=math.dist(vector(p.get_actor_location())[:2],vector(weapon.get_actor_location())[:2])
            if separation<35 and json.loads(LIB.get_validation_state(p))['overlapping_weapon']:
                check('entered_pickup_overlap',True,{'pawn':snapshot(p),'weapon_location':vector(weapon.get_actor_location()),'horizontal_distance_cm':separation})
                advance('equip')
            else:
                if elapsed>8:raise RuntimeError('Could not reach preview weapon pickup')
                for p in state['pawns']:
                    nearest=min(state['pickup_weapons'],key=lambda w:math.dist(vector(p.get_actor_location())[:2],vector(w.get_actor_location())[:2]))
                    offset=nearest.get_actor_location()-p.get_actor_location()
                    p.get_controller().set_control_rotation(unreal.Rotator(yaw=math.degrees(math.atan2(offset.y,offset.x))))
                    inject(p,'move_action',(0,min(.4,max(.2,offset.length()/120)),0))
        elif phase == 'equip':
            if RESTORATION:
                report.setdefault('pickup_samples',[]).append({'elapsed':elapsed,'pawn':snapshot(state['pawns'][0]),
                    'weapons':[{'name':w.get_name(),'location':vector(w.get_actor_location())} for w in state['pickup_weapons']]})
            if elapsed > 1:
                pulse_all('equip_action')
            if elapsed > 3:
                values = [snapshot(p) for p in state['pawns']]
                check('ak_equipped', all(v['native']['ammo'] >= 0 for v in values), values)
                advance('crouch')
        elif phase == 'crouch':
            pulse_all('crouch_action')
            if elapsed > 1:
                check('crouch', all(p.get_editor_property('is_crouched') for p in state['pawns']))
                advance('uncrouch')
        elif phase == 'uncrouch':
            pulse_all('crouch_action')
            if elapsed > 1:
                check('uncrouch', all(not p.get_editor_property('is_crouched') for p in state['pawns']))
                advance('jump')
        elif phase == 'jump':
            pulse_all('jump_action')
            if elapsed > .2:
                check('jump', all(p.character_movement.is_falling() for p in state['pawns']))
                advance('land')
        elif phase == 'land' and elapsed > 2:
            check('land', all(not p.character_movement.is_falling() for p in state['pawns']))
            advance('third_person')
        elif phase == 'third_person':
            pulse_all('toggle_camera_view_action')
            if elapsed > 1:
                check('third_person', all(not p.is_first_person_view() for p in state['pawns']))
                advance('first_person')
        elif phase == 'first_person':
            pulse_all('toggle_camera_view_action')
            if elapsed > 1:
                check('first_person', all(p.is_first_person_view() for p in state['pawns']))
                state.update(player=0, segment=0, point=0, stalled=0)
                report['frame_seconds'] = []
                report['viewport_sizes'] = [list(p.get_controller().get_viewport_size()) for p in state['pawns']]
                state['csv_name'] = 'SpaceCenter_' + OUT.name
                p = state['pawns'][0]
                unreal.SystemLibrary.execute_console_command(p, 'csvprofile STARTFILE=' + state['csv_name'], p.get_controller())
                unreal.SystemLibrary.execute_console_command(p, 'csvprofile START', p.get_controller())
                state['profiling'] = True
                advance('walk')
        elif phase == 'walk':
            p = state['pawns'][state['player']]
            segment = state['route']['segments'][state['segment']]
            report['frame_seconds'].append(delta)
            if len(report['frame_seconds']) % 15 == 0:
                report['samples'].append({'player': state['player'], 'segment': segment['name'], 'time': time.monotonic() - started, **snapshot(p)})
            if RESTORATION and len(report['frame_seconds']) % 150 == 0:
                (OUT/'Progress.json').write_text(json.dumps({'player':state['player']+1,
                    'segment':segment['name'],'point':state['point'],'points':len(segment['points']),
                    'location':vector(p.get_actor_location()),'elapsed_seconds':time.monotonic()-started}),encoding='utf-8')
            if walk(p, segment['points'][state['point']], delta):
                state['point'] += 1
                if state['point'] >= len(segment['points']):
                    state['stalled'] = 0
                    if segment.get('look_at'):
                        p.get_controller().set_control_rotation(unreal.MathLibrary.find_look_at_rotation(p.get_actor_location() + unreal.Vector(0, 0, 65), unreal.Vector(*segment['look_at'])))
                    advance('waypoint')
        elif phase == 'waypoint' and elapsed > 2:
            p = state['pawns'][state['player']]
            segment = state['route']['segments'][state['segment']]
            endpoint = segment['points'][-1]
            values = snapshot(p)
            check('p%d_%s' % (state['player'] + 1, segment['name']),
                  math.dist(values['location'][:2], endpoint[:2]) < 65 and not values['falling'], values)
            if PLAYERS == 2:
                remote = [r for w in LIB.get_validation_worlds() for r in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BrokenEdgeCharacter)
                          if not r.is_locally_controlled() and player_id(r) == player_id(p) and w != p.get_world()]
                check('replicated_p%d_%s' % (state['player'] + 1, segment['name']),
                      len(remote) == 1 and math.dist(vector(remote[0].get_actor_location()), values['location']) < 15
                      and json.loads(LIB.get_validation_state(remote[0]))['ammo'] == values['native']['ammo'],
                      [snapshot(r) for r in remote])
            capture('p%d_%s' % (state['player'] + 1, segment['name']), p)
            print('TRAVERSE_WAYPOINT',state['player']+1,segment['name'],values['location'],flush=True)
            advance('waypoint_capture')
        elif phase == 'waypoint_capture' and elapsed > 2:
            state['segment'] += 1
            state['point'] = 0
            if state['segment'] < len(state['route']['segments']):
                advance('walk')
            elif state['player'] + 1 < PLAYERS:
                advance('park')
            else:
                if HEADLESS:check('headless_runtime_no_render',True,'Rendering is validated by the separate fixed-camera capture stage.')
                else:check('screenshots_written', all((OUT / c).is_file() for c in report['captures']))
                p = state['pawns'][0]
                unreal.SystemLibrary.execute_console_command(p, 'csvprofile STOP', p.get_controller())
                state['profiling'] = False
                advance('profile_flush')
        elif phase == 'profile_flush' and elapsed > 5:
            csv = Path(unreal.Paths.project_saved_dir()) / 'Profiling/CSV' / (state['csv_name'] + '.csv')
            check('engine_profile_written', csv.is_file(), str(csv))
            if csv.is_file():
                shutil.copyfile(csv, OUT / 'Performance.csv')
            finish()
        elif phase == 'park':
            if walk(state['pawns'][state['player']], state['route']['parking'][state['point']], delta):
                state['point'] += 1
                if state['point'] >= len(state['route']['parking']):
                    state.update(player=state['player'] + 1, segment=0, point=0, stalled=0)
                    advance('walk')
        elif phase == 'camera':
            if state['index'] >= len(shots):
                check('captures_written', all((OUT / c).is_file() for c in report['captures']))
                finish()
                return
            name, position, target = shots[state['index']]
            location = unreal.Vector(*position)
            state['camera'].set_actor_location_and_rotation(location, unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(*target)), False, True)
            advance('capture_wait')
        elif phase == 'capture_wait' and elapsed > 5:
            name = shots[state['index']][0] + '.png'
            p = state['pawns'][0]
            unreal.SystemLibrary.execute_console_command(p, 'HighResShot 1280x720 filename=' + str(OUT / name), p.get_controller())
            report['captures'].append(name)
            advance('screenshot')
        elif phase == 'screenshot' and elapsed > 2:
            if not (OUT / report['captures'][-1]).is_file():
                return
            state['index'] += 1
            advance('camera')
    except Exception:
        finish(traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
