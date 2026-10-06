"""Input-driven north traversal, state conservation and separately profiled routes."""
import json
import math
import time


def run(g):
    state, player, send, check, wait = (g[n] for n in ['state', 'player', 'send', 'check', 'wait'])
    root, out, roles, args = (g[n] for n in ['ROOT', 'OUT', 'ROLES', 'args'])
    active = args.rendered_role or 'A'
    peer = 'B' if active == 'A' else 'A'
    g['report']['traversing_role'] = active
    old = json.loads((root / 'Scripts/SpaceCenter/route.json').read_text(encoding='utf-8'))
    segments = {s['name']: s['points'] for s in old['segments']}
    outward = [p for name in ['door', 'stairs_up', 'stairs_down', 'north_exit', 'outside'] for p in segments[name]]
    homeward = segments['room_return']
    north = json.loads((root / 'Scripts/SpaceCenter/north_route.json').read_text(encoding='utf-8'))['points']
    telemetry_at = 0

    def capture_finished(role, name):
        filename = role + '-' + name + '.csv'
        return (out / filename).exists() and any('Capture Ended. Writing CSV to file :' in line and filename in line
            for line in (out / (role + '.log')).read_text(encoding='utf-8', errors='replace').splitlines())

    def walk(role, points, label):
        nonlocal telemetry_at
        assert not send(role, 'walk', points=points)
        end = time.monotonic() + 180
        while time.monotonic() < end:
            current = state(role)
            if args.scenario == 'performance' and time.monotonic() >= telemetry_at:
                telemetry_at = time.monotonic() + 1
                sample = {'monotonic': time.monotonic(), 'route': label,
                          'peers': {p: {k: state(p).get(k) for k in ['physical_mib', 'peak_physical_mib', 'net_in_bytes_per_second', 'net_out_bytes_per_second', 'region']} for p in g['processes']}}
                with (out / 'Telemetry.jsonl').open('a', encoding='utf-8') as log:
                    log.write(json.dumps(sample) + '\n')
            if current.get('error'):
                raise AssertionError(label + ': ' + current['error'])
            if current.get('walk_count') == len(points) and current.get('walk_index') == len(points):
                wait(lambda: not player(role, role).get('falling', True), 'Ground contact after ' + label, 5)
                return
            if g['processes'][role].poll() is not None:
                raise RuntimeError('Client exited during ' + label)
            time.sleep(.15)
        raise TimeoutError(label)

    # Park the second spawn group off the narrow passage; only fixture setup teleports.
    for index, role in enumerate(r for r in roles if r != active):
        send('Server', 'fixture', players=[{'name': 'EX_' + role, 'position': [-2100 - index * 100, -300, 460], 'health': 100, 'shield': 0}])
    wait(lambda: player(active, active).get('position'), 'First pawn ready')
    if args.scenario == 'performance':
        check('Performance viewport is 1920 x 1080', lambda: state(active).get('viewport_width') == 1920 and state(active).get('viewport_height') == 1080)
        check('Runtime medium settings, full resolution, no VSync or frame cap', lambda: len(state(active).get('quality', {})) == 13 and all(value == (1 if key.startswith('sg.') else 100 if key == 'r.ScreenPercentage' else 0) for key, value in state(active)['quality'].items()))
        route = outward + (north + list(reversed(north[:-1])) + [[1400, 4200, 362]] if args.performance_route == 'north' else []) + homeward
        for role in ['Server', active]:
            assert not send(role, 'profile', start=True, name='first-route')
        walk(active, route, 'warmup')
        for role in [active, 'Server']:
            assert not send(role, 'profile', start=False)
        wait(lambda: all(capture_finished(role, 'first-route') for role in [active, 'Server']), 'First visit CSV saved', 45)
        g['report']['first_visit_boundary'] = 'First route in a fresh game process; OS disk cache is not purged. Excluded from subsequent route samples.'
        g['report']['performance_boundary'] = 'Same current packaged map and binaries; old-area route versus north route. One rendered client, one NullRHI peer, dedicated server. No AI in this environmental profile.'
        g['report']['profile_runs'] = []
        for repetition in range(args.performance_samples):
            name = 'performance-' + args.performance_route + '-' + str(repetition + 1)
            for role in ['Server', active]:
                assert not send(role, 'profile', start=True, name=name)
            start = time.monotonic()
            loops = 0
            while time.monotonic() - start < args.performance_seconds:
                walk(active, route, name)
                loops += 1
            for role in [active, 'Server']:
                assert not send(role, 'profile', start=False)
            wait(lambda: all(capture_finished(role, name) for role in [active, 'Server']), 'CSV capture saved', 45)
            entry = {'name': name, 'elapsed_seconds': time.monotonic() - start, 'route_loops': loops,
                     'physical_mib': {role: state(role).get('physical_mib') for role in g['processes']}}
            g['report']['profile_runs'].append(entry)
            g['save']()
            print('PROFILE_DONE', entry, flush=True)
        send(active, 'screenshot', name='performance-final')
        return

    check('Server owns the loaded north region', lambda: state('Server').get('region', {}).get('visible'))
    check('Distant clients unload north', lambda: all(not state(r).get('region', {}).get('loaded', True) for r in roles), 45)
    loot_before = {box['id']: box for box in state('Server').get('loot', [])}
    walk(active, outward, 'old interior and north platform')
    check('Approach preloads north', lambda: state(active)['region']['visible'])
    walk(active, north, 'north route')
    check('Northern position replicates', lambda: math.dist(player(active, active)['position'], player('Server', active)['position']) < 25)
    check('Separate client interests', lambda: state(active)['region']['visible'] and not state(peer)['region']['loaded'])
    check('New ground supports character', lambda: abs(player(active, active)['position'][2] - 144) < 35 and not player(active, active)['falling'])
    send(active, 'screenshot', name='north-route')
    walk(active, list(reversed(north[:-1])) + [[1400, 4200, 362]] + homeward, 'first return')
    check('First unload finishes', lambda: not state(active)['region']['loaded'], 45)
    initial = state(active)['region']
    for repetition in range(args.streaming_loops):
        walk(active, outward + north, 'north loop ' + str(repetition + 1))
        check('Loaded loop ' + str(repetition + 1), lambda: state(active)['region']['visible'])
        walk(active, list(reversed(north[:-1])) + [[1400, 4200, 362]] + homeward, 'return loop ' + str(repetition + 1))
        check('Unloaded loop ' + str(repetition + 1), lambda: not state(active)['region']['loaded'], 45)
    check(str(args.streaming_loops) + ' actual reload and unload transitions', lambda: state(active)['region']['loads'] >= initial['loads'] + args.streaming_loops and state(active)['region']['unloads'] >= initial['unloads'] + args.streaming_loops)
    check('Streaming preserves container identities and items', lambda: {box['id']: box for box in state('Server').get('loot', [])} == loot_before)
    send(active, 'region_delay', seconds=60)
    walk(active, outward, 'delayed approach')
    send(active, 'region_delay', seconds=60)
    send(active, 'walk', points=north[:3])
    time.sleep(7)
    check('Missing region blocks crossing on server', lambda: player('Server', active)['position'][1] < 6000 and not state(active)['region']['visible'])
    send(active, 'walk', points=[])
    check('Slow load exposes the recoverable failure message', lambda: state(active)['region']['status'].startswith('区域加载失败'), 40)
    send(active, 'region_delay', seconds=0)
    check('Delayed region becomes ready', lambda: state(active)['region']['visible'])
    walk(active, north, 'resume after delayed load')
    check('Cooked cross-region navigation paths', lambda: state('Server').get('nav_paths') and all(p['valid'] for p in state('Server')['nav_paths']))
    send(active, 'screenshot', name='streaming-complete')

    # Search and take an actual seeded north supply item through production requests.
    box = next(b for b in state('Server')['loot'] if b['position'][1] > 10000 and not b['loose'])
    walk(active, [[box['position'][0] - 100, box['position'][1], 54]], 'north supply approach')
    send(active, 'open_loot', target=box['id'])
    wait(lambda: player(active, active).get('loot_id') == box['id'], 'North supply opens')
    wait(lambda: any(i['definition'] == 'Tools' for i in player(active, active)['loot_view']['items']), 'North search reveals tools', 15)
    taken = next(i for i in player(active, active)['loot_view']['items'] if i['definition'] == 'Tools')
    send(active, 'item', item=taken['id'], from_loot=True, quantity=1)
    check('North supply transfers one original item identity', lambda: any(i['id'] == taken['id'] for i in player('Server', active)['inventory']['items'])
          and not any(i['id'] == taken['id'] for b in state('Server')['loot'] for i in b['contents']['items']))
    send(active, 'inventory', open=False)
    walk(active, list(reversed(north)) + [[1400, 4200, 362]] + homeward, 'return carrying north loot')
    check('North loot survives unloading the region', lambda: not state(active)['region']['loaded'] and any(i['id'] == taken['id'] for i in player('Server', active)['inventory']['items']))

    # Keep the spectator outside its interest radius before the target crosses.
    mate = peer if len(roles) == 2 else 'C'
    send('Server', 'fixture', players=[{'name': 'EX_' + mate, 'position': [-1500, -300, 460], 'health': 100, 'shield': 0}])
    exit_at = state('Server')['exit_position']
    send('Server', 'fixture', players=[{'name': 'EX_' + active, 'position': [exit_at[0], exit_at[1], exit_at[2] + 90], 'health': 100, 'shield': 0}])
    check('North loot extracts into the persistent stash', lambda: state(active)['account']['outcome'] == 1 and any(i['id'] == taken['id'] for i in state(active)['account']['stash']['items']), 22)
    wait(lambda: state(active).get('spectated') == 'EX_' + mate, 'Spectator selects living squadmate')
    check('Spectator unloads while mate remains in old interior', lambda: not state(active)['region']['loaded'], 45)
    send(active, 'region_delay', seconds=60)
    walk(mate, outward, 'spectated mate approaches north')
    send(active, 'region_delay', seconds=60)
    walk(mate, north, 'spectated mate crosses to north')
    check('Spectator requests the remote region independently', lambda: state(active)['region']['requested'])
    check('Spectator holds its previous view until north is ready', lambda: not state(active)['region']['visible'] and state(active)['view_position'][1] < 6000)
    send(active, 'region_delay', seconds=0)
    check('Cross-region spectator becomes ready', lambda: state(active)['region']['visible'] and state(active).get('spectated') == 'EX_' + mate and state(active)['view_position'][1] > 6000)
    send(active, 'screenshot', name='north-spectator')
    walk(mate, list(reversed(north[:-1])) + [[1400, 4200, 362]] + homeward, 'spectated return')
    check('Spectator releases north after mate returns', lambda: not state(active)['region']['loaded'], 45)
    send('Server', 'end')
    check('Streaming round returns every peer to lobby', lambda: all(state(p).get('map') == '/Game/Maps/Lobby' for p in g['processes']), 40)
    check('Exported north item remains after lobby travel', lambda: any(i['id'] == taken['id'] for i in state(active)['account']['stash']['items']))
    g['begin_round']()
    check('Second round creates a fresh server streaming manager', lambda: state('Server')['region']['visible'] and state('Server')['region']['loads'] == 1)
    check('Second round does not duplicate exported north identity', lambda: not any(i['id'] == taken['id'] for b in state('Server')['loot'] for i in b['contents']['items']))
    send('Server', 'end')
    check('Second streaming round returns normally', lambda: all(state(p).get('map') == '/Game/Maps/Lobby' for p in g['processes']), 40)
