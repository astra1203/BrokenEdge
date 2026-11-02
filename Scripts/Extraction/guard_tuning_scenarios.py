"""Live easy-guard regression: timing, real bullets, smoke, reload and looted weapon."""
import time
import hashlib
import math
from pathlib import Path


def guard_interval(rules, weapon):
    """Use the same runtime limits as BTTask_EngageTarget, with no stale defaults."""
    delay, override = weapon['fire_delay'], rules['shot_interval']
    assert math.isfinite(delay) and delay > 0, 'Missing or invalid guard weapon FireDelay'
    assert math.isfinite(override), 'Invalid guard shot interval'
    return max(delay, max(0, override))


def collect_shots(events, guard, baseline):
    """Merge authority events; a driver snapshot may contain several new bullets."""
    count = guard['shots']
    assert isinstance(count, (int, float)) and math.isfinite(count) and count == int(count), 'Invalid shot count'
    count = int(count)
    assert count >= baseline, 'Guard shot counter reset during capture'
    for event in guard['shot_times']:
        sequence, at = event['sequence'], event['time']
        assert isinstance(sequence, (int, float)) and math.isfinite(sequence), 'Invalid shot sequence'
        assert sequence == int(sequence) and 0 < sequence <= count, 'Invalid shot sequence'
        sequence = int(sequence)
        assert math.isfinite(at) and at >= 0, 'Invalid server shot time'
        if sequence <= baseline:
            continue
        assert sequence not in events or events[sequence] == at, 'Authoritative shot timestamp changed'
        events[sequence] = at
    assert all(sequence in events for sequence in range(baseline + 1, count + 1)), (
        'Authoritative shot history is incomplete; capture exceeded the 64-shot history window')


def validate_timing(events, baseline, rules, weapon, reaction_ready_time):
    """Validate actual accepted shots, not 0.1-second JSON observation times."""
    interval = guard_interval(rules, weapon)
    reaction = rules['reaction_seconds']
    assert math.isfinite(reaction) and math.isfinite(reaction_ready_time), 'Invalid reaction timing'
    reaction = max(0, reaction)
    sequences = sorted(events)
    assert len(sequences) >= 5, 'Need at least five accepted guard shots'
    assert sequences == list(range(baseline + 1, sequences[-1] + 1)), 'Missing accepted shot history'
    times = [events[sequence] for sequence in sequences]
    intervals = [b - a for a, b in zip(times, times[1:])]
    # The driver launches at 30 FPS. Allow one frame plus scheduling jitter, not
    # an extra snapshot period; the timestamps are taken in the shot RPC itself.
    assert all(interval - .001 <= value <= interval + .05 for value in intervals), (
        f'Guard cadence differs from runtime interval {interval:.4f}s: {intervals}')
    assert reaction_ready_time - .001 <= times[0] <= reaction_ready_time + .4, (
        'First guard shot is outside the server reaction deadline')
    return dict(reaction_seconds=times[0] - (reaction_ready_time - reaction),
                expected_reaction_seconds=reaction, expected_interval_seconds=interval,
                shot_intervals=intervals, shot_sequences=sequences, server_shot_times=times)


def validate_damage(hits, rules, weapon):
    """Unarmored fixture hits retain weapon damage, scaled only by the attacker."""
    scale = rules['damage_scale']
    assert math.isfinite(scale), 'Invalid guard damage scale'
    scale = min(1, max(0, scale))
    assert len(hits) >= 2 and any(hit['bone'].lower() != 'head' for hit in hits), (
        'Need at least two observed guard hits including a body hit')
    for hit in hits:
        base = weapon['head_damage'] if hit['bone'].lower() == 'head' else weapon['damage']
        assert math.isfinite(base) and base > 0, 'Invalid guard weapon damage'
        expected = base * scale
        assert math.isfinite(hit['loss']) and abs(hit['loss'] - expected) < .01, (
            f'Guard {hit["bone"]} damage {hit["loss"]} differs from {base} x {scale} = {expected}')
    return True


def select_guard(guards):
    """Guard IDs follow actor object ordering; select an authored central route."""
    candidates = [guard for guard in guards if guard['route'] in ('ExtractionPatrol_0', 'ExtractionPatrol_1')]
    assert candidates, 'No guard owns an authored central patrol route'
    return min(candidates, key=lambda guard: (guard['route'], guard['name']))


def reserve_ammo(inventory, ammo_type='Ammo'):
    return sum(item['quantity'] for item in inventory['items'] if item['definition'] == ammo_type)


def validate_reload(before, after, shots_key):
    """Every removed reserve round must remain loaded or have been fired."""
    fired = after[shots_key] - before[shots_key]
    consumed = before['carried_ammo'] - after['carried_ammo']
    assert before['ammo'] == 0 and before['carried_ammo'] > 0, 'Reload needs an observed empty magazine and reserve'
    assert after['ammo'] > 0 and after['combat_state'] == 0, 'Reload must finish with live ammunition'
    assert fired >= 0 and consumed > 0 and consumed == after['ammo'] + fired, (
        f'Reload ammunition is not conserved: reserve consumed={consumed}, magazine={after["ammo"]}, fired={fired}')
    return True


def reload_fixture_positions(patrol_position):
    """Face the observed patrol side; this is a placement hint, not measured AI yaw."""
    assert len(patrol_position) == 3 and all(math.isfinite(v) for v in patrol_position), 'Invalid patrol position'
    ends = [[1400, 3900, 500], [1400, 4500, 500]]
    player_position, guard_position = sorted(ends, key=lambda point: math.dist(point[:2], patrol_position[:2]))
    yaw = math.degrees(math.atan2(player_position[1] - guard_position[1],
                                  player_position[0] - guard_position[0]))
    return player_position, guard_position, yaw


def reload_target_ready(snapshot, guard_name, after_time, previous_reaction):
    """Require one fresh authority snapshot proving acquisition, not just clear geometry."""
    current = next(v for v in snapshot['guards'] if v['name'] == guard_name)
    victim = next(v for v in snapshot['players'] if v['name'] == 'EX_A')
    return all((snapshot['world_time'] > after_time, not snapshot['smokes'],
                not current['dead'], current['health'] > 0, current['state'] == 1,
                victim['life'] == 0, victim['health'] > 0,
                current['target'] == victim['pawn_name'], current['line_of_sight_to_A'],
                not current['smoke_blocked_to_A'], current['combat_state'] == 0,
                current['ammo'] > 0, current['carried_ammo'] > 0,
                math.isfinite(current['reaction_ready_time']),
                current['reaction_ready_time'] > previous_reaction))


def prepare_reload_target(place_fixture, read_snapshot, guard_name, patrol_position, evidence,
                          max_attempts=6, attempt_seconds=1.5):
    """Only place actors during bounded preparation; never force targets or reloads."""
    from copy import deepcopy

    assert isinstance(max_attempts, int) and max_attempts > 0
    assert math.isfinite(attempt_seconds) and attempt_seconds > 0
    initial = read_snapshot()
    original = next(v for v in initial['guards'] if v['name'] == guard_name)
    player_position, guard_position, yaw = reload_fixture_positions(patrol_position)
    evidence.update(started_world_time=initial['world_time'],
                    previous_reaction_ready_time=original['reaction_ready_time'],
                    patrol_reference_position=list(patrol_position),
                    orientation_basis='Observed patrol position; requested yaw only, not measured AI facing',
                    max_attempts=max_attempts, attempt_seconds=attempt_seconds, attempts=[])

    def capture(snapshot):
        current = next(v for v in snapshot['guards'] if v['name'] == guard_name)
        victim = next(v for v in snapshot['players'] if v['name'] == 'EX_A')
        return deepcopy(dict(world_time=snapshot['world_time'], command=snapshot['command'],
                             guards=[current], smokes=snapshot['smokes'], players=[{
                                 key: victim[key] for key in ('name', 'pawn_name', 'position', 'life', 'health')}]))

    def acquired(snapshot):
        return reload_target_ready(snapshot, guard_name, evidence['started_world_time'],
                                   evidence['previous_reaction_ready_time'])

    for attempt in range(1, max_attempts + 1):
        before = capture(read_snapshot())
        if acquired(before):
            evidence['acquired'] = before
            return before
        current, victim = before['guards'][0], before['players'][0]
        assert not current['dead'] and victim['life'] == 0, 'Reload preparation cannot revive defeated actors'
        assert current['ammo'] > 0 and current['carried_ammo'] > 0, 'Reload acquisition needs a loaded guard and reserve'
        actors = [dict(name=name, position=position, health=100, shield=0, clear_trauma=True, yaw=yaw)
                  for name, position in [('EX_A', player_position), (guard_name, guard_position)]]
        record = dict(attempt=attempt, requested_players=deepcopy(actors), before=before)
        evidence['attempts'].append(record)
        # One server command prevents movement between the two placements. AI
        # path following stays active and may still override the requested yaw.
        place_fixture(actors)
        deadline = time.monotonic() + attempt_seconds
        while time.monotonic() < deadline:
            observed = capture(read_snapshot())
            record['last'] = observed
            if acquired(observed):
                evidence['acquired'] = observed
                return observed
            time.sleep(.05)
    raise AssertionError(f'Guard reload acquisition prerequisite failed after {max_attempts} fixture attempts')


def raid_item_owners(snapshot, item_id):
    """Count live raid ownership once, excluding replicated owner copies and account escrow."""
    containers = [('player:' + p['name'], p['inventory']) for p in snapshot['players']]
    containers += [('guard:' + guard['name'], guard['inventory']) for guard in snapshot['guards']]
    containers += [('loot:' + box['id'], box['contents']) for box in snapshot['loot']]
    return [owner for owner, inventory in containers for item in inventory['items'] if item['id'] == item_id]


def guard_loot_ready(server_player, owner_player, box, gun):
    """Require the searched original item, matching sessions/revisions, and a free slot."""
    source = [item for item in box['contents']['items'] if item['id'] == gun]
    if len(source) != 1 or source[0]['definition'] != 'AK' or source[0]['quantity'] != 1:
        return False
    return all((server_player['inventory'] == owner_player['inventory'],
                server_player['session'] == owner_player['session'], server_player['session'] > 0,
                *[p['inventory_open'] and p['loot_id'] == box['id'] and p['search_end'] == 0
                  and p['loot_view'] == box['contents']
                  and not any(item['slot'] == 1 for item in p['inventory']['items'])
                  for p in (server_player, owner_player)]))


def request_item_with_receipt(send_item, read_owner, wait, request, receipts):
    """Save the first owner RPC reply before its timed UI message can disappear."""
    before = read_owner()
    previous = before['item_sequence']
    record = dict(request=dict(request), previous_sequence=previous)
    receipts.append(record)
    send_item(**request)

    def received():
        snapshot = read_owner()
        if snapshot['item_sequence'] <= previous:
            return False
        record['reply'] = {key: snapshot[key] for key in ('world_time', 'item_sequence', 'item_message')}
        return True

    wait(received, 'owner item-operation receipt', 10)
    reply = record['reply']
    assert reply['item_sequence'] == previous + 1, 'Unexpected concurrent item-operation sequence'
    assert not reply['item_message'], f'Item operation rejected: {reply["item_message"]}'
    return record


def run(g):
    g['report']['input_sha256']['Scripts/Extraction/guard_tuning_scenarios.py'] = hashlib.sha256(
        Path(__file__).read_bytes()).hexdigest()
    send, check, wait, state, player = (g[k] for k in ['send', 'check', 'wait', 'state', 'player'])
    peers = ['Server', 'A', 'B']
    assert all(peer in g['processes'] for peer in peers), 'Guard fixture requires a server and clients A/B'
    evidence = g['report']['guard_tuning'] = dict(
        scope='Development position/ammo/exit fixtures with real AI, combat, loot and settlement RPCs; '
              'not continuous traversal, authored exit timing or performance acceptance')

    def command(role, op, **fields):
        error = send(role, op, **fields)
        assert not error, f'{role} {op}: {error}'

    def place(name, position, health=100, yaw=90):
        command('Server', 'fixture', players=[dict(name=name, position=position, health=health,
                                                 shield=0, clear_trauma=True, yaw=yaw)])

    # Reuse authored points whose complete nav paths are currently valid. The
    # old (5700,5000) parking is inside the current 75 m loss-of-sight radius.
    initial_guards = state('Server')['guards']
    nav_points = {tuple(path['start']) for path in state('Server')['nav_paths'] if path['valid']}
    hazards = [v['position'] for v in initial_guards] + [[1400, 4200, 500], [-4300, -6300, 180]]
    safe_points = sorted((point for point in nav_points if all(math.dist(point, hazard) > 12000
                         for hazard in hazards)), key=lambda point: tuple(point))
    assert len(safe_points) >= 2, 'Need two navigable parking points outside guard sight and pursuit'
    safe_a = safe_points[0]
    safe_b = next(point for point in safe_points[1:] if math.dist(point, safe_a) > 250)
    evidence['safe_player_floor_points'] = [safe_a, safe_b]

    def safe_players():
        for role, point in [('A', safe_a), ('B', safe_b)]:
            place('EX_' + role, [point[0], point[1], point[2] + player('Server', role)['capsule_half'] + 2])

    safe_players()  # Keep current long-range sight from interrupting the patrol prerequisite.
    check('Four distinct 100 HP guards exist on authority', lambda:
          len(state('Server')['guards']) == 4
          and len({v['name'] for v in state('Server')['guards']}) == 4
          and all(v['health'] == 100 for v in state('Server')['guards']))
    check('All four guards reach patrol points on authority', lambda:
          len(state('Server')['guards']) == 4
          and all(v['arrivals'] >= 2 for v in state('Server')['guards']), 100)
    chosen = select_guard(state('Server')['guards'])
    guard_name = chosen['name']
    evidence['selected_guard'] = {key: chosen[key] for key in ('name', 'route', 'route_object', 'position', 'arrivals')}

    def guard():
        return next(v for v in state('Server')['guards'] if v['name'] == guard_name)

    parking = []
    for point in sorted(nav_points, reverse=True):
        if (math.dist(point, [1400, 4200, 500]) > 12000
                and all(math.dist(point, safe) > 12000 for safe in (safe_a, safe_b))
                and all(math.dist(point, previous) > 250 for previous in parking)):
            parking.append(point)
        if len(parking) == 3:
            break
    assert len(parking) == 3, 'Need three separate navigable non-target guard parking points'
    evidence['other_guard_floor_points'] = parking

    def park_other_guards():
        for point, other in zip(parking, (v for v in state('Server')['guards'] if v['name'] != guard_name)):
            place(other['name'], [point[0], point[1], point[2] + player('Server', 'A')['capsule_half'] + 2])

    def targets_a():
        return guard()['target'] == player('Server', 'A')['pawn_name']

    def item_operation(**request):
        wait(lambda: player('Server', 'A')['inventory'] == player('A', 'A')['inventory'],
             'owner inventory matches authority before item operation')
        receipt = request_item_with_receipt(
            lambda **fields: command('A', 'item', **fields), lambda: state('A'), wait,
            request, evidence.setdefault('item_operations', []))
        wait(lambda: player('Server', 'A')['inventory'] == player('A', 'A')['inventory'],
             'owner inventory matches authority after item operation')
        return receipt

    def clear_secondary_slot():
        wait(lambda: player('Server', 'A')['inventory'] == player('A', 'A')['inventory'],
             'secondary-slot preparation has synchronized inventory')
        occupied = [item for item in player('Server', 'A')['inventory']['items'] if item['slot'] == 1]
        assert len(occupied) <= 1, 'Multiple weapons already occupy the secondary slot'
        record = evidence['secondary_slot_preparation'] = dict(occupied_before=occupied)
        for original in occupied:
            ident = original['id']
            record['owners_before'] = raid_item_owners(state('Server'), ident)
            assert record['owners_before'] == ['player:EX_A'], 'Original secondary weapon has duplicate ownership'
            record['receipt'] = item_operation(item=ident, quantity=1, slot=-1, auto=True)
            check('Original secondary weapon moves into storage before guard-rifle pickup', lambda: all(
                  not any(item['slot'] == 1 for item in player(peer, 'A')['inventory']['items'])
                  and any(item['id'] == ident and item['slot'] < 0
                          for item in player(peer, 'A')['inventory']['items']) for peer in ('Server', 'A')))
            moved = next(item for item in player('Server', 'A')['inventory']['items'] if item['id'] == ident)
            payload = lambda item: {key: value for key, value in item.items() if key not in ('slot', 'x', 'y', 'rotated')}
            assert payload(original) == payload(moved), 'Storing the original weapon changed its identity or contents'
            record['moved'] = moved
            record['owners_after'] = raid_item_owners(state('Server'), ident)
            assert record['owners_after'] == ['player:EX_A'], 'Stored original weapon has duplicate ownership'

    def full_damage():
        command('Server', 'fixture', players=[dict(name='EX_' + role, position=position,
                health=80, shield=0, clear_trauma=True) for role, position in
                [('A', [1400, 4200, 500]), ('B', [1400, 3900, 500])]])
        time.sleep(1)
        command('A', 'aim', target='EX_B', head=False)
        time.sleep(.8)
        for _ in range(12):
            before = player('Server', 'B').copy()
            assert before['life'] == 0, 'Damage fixture cannot revive a downed/dead player by setting health'
            assert not any(i['slot'] in (2, 3) for i in before['inventory']['items']), 'Damage victim must be unarmored'
            shots = player('Server', 'A')['accepted']
            command('A', 'pulse', action='Fire', duration=0)
            wait(lambda: player('Server', 'A')['accepted'] == shots + 1, 'one player bullet')
            time.sleep(.35)
            after = player('Server', 'B')
            if after['bullet_sequence'] > before['bullet_sequence'] and after['hit_loss'] == 20:
                check('Player body bullet deals 20 HP on all peers', lambda: all(
                    abs(player(p, 'B')['health'] - after['health']) <= 1 for p in peers))
                return
            place('EX_B', before['position'], health=80)
            time.sleep(.3)
        raise AssertionError('No normal 20 HP body hit after 12 scatter samples')

    safe_players()
    park_other_guards()
    # An NPC outside a client's relevancy distance need not exist on that client.
    # Test replication after this selected central guard and both observers approach.
    place(guard_name, [1400, 3900, 500])
    wait(lambda: guard()['target'] == 'None', 'guard has no target')
    before = int(guard()['shots'])
    place('EX_A', [1400, 4500, 500])
    wait(targets_a, 'guard acquires player A')
    place('EX_B', [1400, 5500, 500])
    initial = state('Server')
    rules = initial['guard_rules']
    weapon = next(v for v in initial['guards'] if v['name'] == guard_name)
    reaction_ready = weapon['reaction_ready_time']
    guard_interval(rules, weapon)
    assert weapon['ammo'] >= 5, 'Cadence capture requires five loaded shots before any reload'
    assert next(i for i in weapon['inventory']['items'] if i['slot'] == 0)['ammo_type'] == 'Ammo'
    assert not any(i['slot'] in (2, 3) for i in player('Server', 'A')['inventory']['items'])
    events, hits = {}, []
    evidence.update(rules=rules, weapon={key: weapon[key] for key in ('fire_delay', 'damage', 'head_damage')},
                    reaction_ready_time=reaction_ready, shot_events=events, hits=hits)
    other_shots = {v['name']: v['shots'] for v in initial['guards'] if v['name'] != guard_name}
    sequence = player('Server', 'A')['bullet_sequence']
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        snap = state('Server')
        current = next(v for v in snap['guards'] if v['name'] == guard_name)
        victim = next(v for v in snap['players'] if v['name'] == 'EX_A')
        assert snap['guard_rules'] == rules, 'Guard rules changed during capture'
        assert all(current[key] == weapon[key] for key in ('fire_delay', 'damage', 'head_damage'))
        assert current['reaction_ready_time'] == reaction_ready, 'Guard reacquired a target during cadence capture'
        assert all(v['shots'] == other_shots[v['name']] for v in snap['guards'] if v['name'] != guard_name), (
            'Another guard fired during the isolated damage fixture')
        collect_shots(events, current, before)
        if victim['bullet_sequence'] != sequence:
            hits.append(dict(loss=victim['hit_loss'], bone=victim['hit_bone'], sequence=victim['bullet_sequence']))
            sequence = victim['bullet_sequence']
        if len(events) >= 5 and len(hits) >= 2 and any(v['bone'].lower() != 'head' for v in hits):
            break
        time.sleep(.025)
    check('Nearby selected 100 HP guard replicates to both clients', lambda: all(
          any(v['name'] == guard_name and v['health'] == 100 for v in state(peer)['guards']) for peer in peers))
    safe_players()

    def timing_check():
        # Damage scatter may need later shots, including a legitimate reload.
        # The first five loaded shots are the continuous cadence/reaction sample.
        first_five = {key: events[key] for key in sorted(events)[:5]}
        evidence.update(validate_timing(first_five, before, rules, weapon, reaction_ready))
        return True

    check('First five accepted guard shots respect server reaction and weapon/rule interval', timing_check)
    check('Observed guard hits apply the server GuardDamageScale to real weapon damage',
          lambda: validate_damage(hits, rules, weapon))

    place(guard_name, [parking[0][0], parking[0][1], parking[0][2] + player('Server', 'A')['capsule_half'] + 2])
    full_damage()
    safe_players()
    park_other_guards()

    command('Server', 'occluder', enabled=True)
    place('EX_A', [1800, 4200, 500])
    place(guard_name, [1400, 4200, 500], yaw=0)
    wait(lambda: guard()['target'] == 'None' and not guard()['line_of_sight_to_A'], 'wall blocks acquisition')
    assert guard()['ammo'] > 0 and guard()['combat_state'] == 0, 'Wall fixture needs a fireable guard weapon'
    before = guard()['shots']
    for _ in range(12):
        place(guard_name, [1400, 4200, 500], yaw=0)
        time.sleep(.15)
        assert guard()['shots'] == before and not guard()['line_of_sight_to_A'], 'Wall fixture lost occlusion'
    check('Opaque wall prevents shooting', lambda: guard()['shots'] == before)
    command('Server', 'occluder', enabled=False)
    safe_players()
    place(guard_name, [1400, 3900, 500])
    wait(lambda: guard()['target'] == 'None', 'wall fixture target clears')
    place('EX_A', [1400, 4500, 500])
    wait(lambda: targets_a() and guard()['line_of_sight_to_A'] and not guard()['smoke_blocked_to_A'],
         'Visible player is acquired before smoke')
    # Cast during the fresh reaction window: actual bullet damage cancels abilities.
    # Throw down along +Y, ahead of A, so relocating A into the resulting smoke
    # keeps A in front of the +Y-facing guard instead of creating a rear-FOV pass.
    command('A', 'look', yaw=90, pitch=-75)
    command('A', 'ability', kind='smoke')
    wait(lambda: bool(state('Server')['smokes']), 'smoke deploys after flight, settle and fuse', 8)
    smoke = state('Server')['smokes'][0]
    centre = smoke['position']
    place('EX_A', [centre[0], centre[1] + 200, centre[2] + player('Server', 'A')['capsule_half']])
    check('Smoke geometry blocks otherwise clear guard vision and removes target', lambda:
          guard()['line_of_sight_to_A'] and guard()['smoke_blocked_to_A'] and guard()['target'] == 'None'
          and guard()['ammo'] > 0 and guard()['combat_state'] == 0, 5)
    before = guard()['shots']
    evidence['smoke'] = dict(position=centre, radius=smoke['radius'], expires=smoke['expires'], shots=before)
    smoke_start = state('Server')['world_time']
    timeout = time.monotonic() + 12
    while state('Server')['world_time'] < smoke_start + 3:
        assert time.monotonic() < timeout, 'Server stopped advancing during smoke hold'
        current = guard()
        assert state('Server')['smokes'], 'Smoke expired during its required hold'
        assert current['line_of_sight_to_A'] and current['smoke_blocked_to_A'], 'Smoke isolation geometry changed'
        assert current['ammo'] > 0 and current['combat_state'] == 0, 'Smoke must block a loaded, idle weapon'
        assert current['target'] == 'None' and current['shots'] == before, 'Guard fired or targeted through smoke'
        time.sleep(.05)
    check('Guard does not shoot through smoke for three server seconds', lambda: guard()['shots'] == before)
    safe_players()
    wait(lambda: not state('Server')['smokes'], 'smoke expires', 25)
    wait(lambda: guard()['target'] == 'None' and guard()['combat_state'] == 0, 'guard idle before empty-magazine fixture')
    preparation = evidence['reload_preparation'] = {}
    acquired = prepare_reload_target(
        lambda actors: command('Server', 'fixture', players=actors), lambda: state('Server'),
        guard_name, chosen['position'], preparation)
    check('Loaded guard reacquires A with clear sight before the empty-magazine fixture', lambda:
          reload_target_ready(acquired, guard_name, preparation['started_world_time'],
                              preparation['previous_reaction_ready_time']))
    # Preparation is finished. Through reload completion, do not reposition
    # actors or interfere with AI; only empty the verified magazine.
    command('Server', 'ammo', target=guard_name, value=0)
    wait(lambda: guard()['ammo'] == 0, 'guard empty magazine is authoritative')
    reload_before = guard()
    evidence['guard_reload'] = dict(before=reload_before)
    assert reload_before['carried_ammo'] == reserve_ammo(reload_before['inventory'])
    wait(lambda: guard()['combat_state'] == 1, 'acquired guard enters real reload', 12)
    evidence['guard_reload']['entered'] = guard()
    check('Guard reacquires and fires after reloading', lambda:
          guard()['combat_state'] == 0 and guard()['ammo'] > 0 and guard()['shots'] > reload_before['shots'], 12)
    reload_after = guard()
    check('Guard reload transfers reserve into the magazine and subsequent real shots',
          lambda: validate_reload(reload_before, reload_after, 'shots')
          and reload_after['carried_ammo'] == reserve_ammo(reload_after['inventory']))
    evidence['guard_reload']['after'] = reload_after
    safe_players()
    park_other_guards()

    wait(lambda: guard()['target'] == 'None' and guard()['combat_state'] == 0, 'guard idle before kill fixture')
    command('Server', 'ammo', target=guard_name, value=0)
    place(guard_name, [1400, 4200, 500], health=20)
    place('EX_A', [1400, 3900, 500])
    command('A', 'aim', target=guard_name, head=False)
    gun = next(v for v in guard()['inventory']['items'] if v['definition'] == 'AK')['id']
    for _ in range(10):
        command('A', 'pulse', action='Fire', duration=0)
        time.sleep(.4)
        if guard()['dead']:
            break
    check('Guard can be killed by player fire', lambda: guard()['dead'])

    def dropped_boxes():
        return [v for v in state('Server')['loot'] if any(i['id'] == gun for i in v['contents']['items'])]

    check('Guard drops exactly one original weapon', lambda: len(dropped_boxes()) == 1)
    box = dropped_boxes()[0]
    place('EX_A', [box['position'][0] + 100, box['position'][1], box['position'][2] + 50])
    command('A', 'aim', target='', head=False)
    wait(lambda: any(v['target'] == box['id'] and v['distance'] <= 200 and v['clear']
                     for v in player('Server', 'A')['reach']), 'Dropped rifle container is truly reachable')
    clear_secondary_slot()  # Move the existing SCAR-H through the normal inventory RPC; never discard it.
    command('A', 'open_loot', target=box['id'])
    wait(lambda: guard_loot_ready(player('Server', 'A'), player('A', 'A'), dropped_boxes()[0], gun),
         'searched guard rifle, container session/revision and empty secondary slot are synchronized', 15)
    evidence['rifle_pickup_precondition'] = dict(
        server=player('Server', 'A')['loot_view'], owner=player('A', 'A')['loot_view'],
        inventory=player('A', 'A')['inventory'], session=player('A', 'A')['session'])
    item_operation(item=gun, from_loot=True, quantity=1, slot=1)
    check('Original rifle is in player secondary slot on authority and owner', lambda: all(
          any(i['id'] == gun and i['slot'] == 1 for i in player(peer, 'A')['inventory']['items'])
          for peer in ('Server', 'A')))
    assert not dropped_boxes(), 'Original rifle still exists in a ground container after transfer'
    command('A', 'inventory', open=False)
    command('A', 'swap')
    check('Player equips looted rifle', lambda: player('Server', 'A')['weapon_item'] == gun)
    wait(lambda: player('Server', 'A')['combat_state'] == 0, 'weapon swap finishes')
    # The guard may have finished a reload before death. Explicitly empty this
    # same item before proving the player's normal reload RPC, rather than
    # treating an already-loaded looted gun as a successful reload.
    command('Server', 'ammo', target='EX_A', value=0)
    wait(lambda: player('Server', 'A')['ammo'] == 0, 'looted rifle empty magazine fixture')
    player_reload_before = player('Server', 'A')
    command('A', 'pulse', action='Reload', duration=0)
    wait(lambda: player('Server', 'A')['combat_state'] == 1, 'looted rifle enters real player reload')
    check('Looted rifle reloads normally', lambda:
          player('Server', 'A')['combat_state'] == 0 and player('Server', 'A')['ammo'] > 0)
    player_reload_after = player('Server', 'A')
    check('Player reload conserves reserve and loaded rounds',
          lambda: validate_reload(player_reload_before, player_reload_after, 'accepted'))
    evidence['player_reload'] = dict(before=player_reload_before, after=player_reload_after)
    full_damage()
    check('Looted guard rifle retains full player damage', lambda: player('Server', 'B')['hit_loss'] == 20)
    command('A', 'screenshot', name='easy-guards-looted-rifle')

    selected_exit = next(v for v in state('Server')['exits'] if v['id'] == 'OfficialExitSquare')
    evidence['exit_fixture'] = dict(original=selected_exit, opens_after=0, closes_after=0,
                                  hold_seconds=selected_exit['hold_seconds'])
    command('Server', 'exit_config', exit_id=selected_exit['id'], opens_after=0, closes_after=0)
    wait(lambda: all(any(v['id'] == selected_exit['id'] and v['available'] and v['opens_after'] == 0
                         and v['closes_after'] == 0 for v in state(peer)['exits']) for peer in peers),
         'Development plaza opening fixture replicates')
    assert not any(i['id'] == gun for i in state('A')['account']['stash']['items'])
    place('EX_A', selected_exit['bounds_center'])
    check('Looted rifle starts an owner-only plaza extraction hold', lambda: all(
          player(peer, 'A')['extract_exit_id'] == selected_exit['id']
          and player(peer, 'A')['extract_at'] > 0 for peer in ('Server', 'A')))
    check('Looted rifle survives real extraction settlement', lambda: player('Server', 'A')['life'] == 3
          and any(i['id'] == gun for i in state('A')['account']['stash']['items']),
          selected_exit['hold_seconds'] + 15)
    command('Server', 'end')
    check('Raid returns to lobby', lambda: all(state(p)['map'] == '/Game/Maps/Lobby' for p in peers), 40)
