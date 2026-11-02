"""Independent fixture reconstruction of route02/flight01 camera-to-body geometry.

run(g) owns normal lobby AK preparation and ready/spawn; the runner must skip
begin_round(), use a fresh OUT/store/profile pair, --guards 0, clients A/B and
--flight-diagnostics. No Fire, ammo, damage or AI commands are issued. The legacy
fixture API requires health/shield: their current values are passed unchanged.
B is a same-class player proxy, never the historical guard or pose.
"""
import copy
import hashlib
import json
import math
from pathlib import Path

MAP = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction'
ROLES = ('A', 'B')
POSITION_TOLERANCE_CM = 2.0
CAMERA_TOLERANCE_CM = .25
SAMPLES_PER_CASE = 3
RESOURCE_FIELDS = ('pawn_name', 'pawn_class', 'health', 'shield', 'ammo', 'carried_ammo',
                   'accepted', 'rejected', 'bullet_sequence', 'guard_kills', 'life',
                   'bleeding', 'fracture', 'weapon_item')
QUERIES = ('camera_simple', 'camera_complex', 'muzzle_complex_sphere')


def vector(value):
    assert isinstance(value, (list, tuple)) and len(value) == 3, 'Expected a 3D vector'
    assert all(isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)
               for x in value), 'Vector contains a non-finite coordinate'
    return list(value)


def read_complete_records(text):
    """An unfinished append is ignored; a malformed complete line is an error."""
    complete = text[:text.rfind('\n') + 1]
    return [json.loads(line) for line in complete.splitlines() if line.strip()]


def load_history(root):
    names = {
        'old_camera': 'Saved/Extraction/overnight-route02/A-state.json',
        'old_authority': 'Saved/Extraction/overnight-route02/Server-state.json',
        'new_route': 'Saved/Extraction/overnight-route-flight01/Report.json',
        'new_probes': 'Saved/Extraction/overnight-route-flight01/A-aim-diagnostics.jsonl',
    }
    raw = {key: (root / name).read_bytes() for key, name in names.items()}
    old_client = json.loads(raw['old_camera'].decode('utf-8-sig'))
    old_server = json.loads(raw['old_authority'].decode('utf-8-sig'))
    new_report = json.loads(raw['new_route'].decode('utf-8-sig'))
    old_a = next(p for p in old_server['players'] if p['name'] == 'EX_A')
    old_guard = next(p for p in old_server['guards'] if p['name'] == 'GUARD_6')
    event = new_report['overnight_route']['encounters'][0]
    records = read_complete_records(raw['new_probes'].decode('utf-8-sig'))
    assert old_a['accepted'] == 90 and old_guard['health'] == 100, 'Wrong old encounter source'
    assert event['result'] == 'passed' and event['accepted_shots'] == 10, 'Wrong new encounter source'
    assert len(records) >= 2, 'A settled successful-run camera sample is required'
    probe = records[1]['aim_diagnostics']
    assert probe['target_name'] == 'GUARD_6' and not probe['configured_head']
    assert probe['configured_bone'] == 'None' and not probe['configured_body_center']
    body = vector(probe['points']['body']['position'])
    history = {
        'old': dict(camera=vector(old_client['view_position']), actor=vector(old_a['position']),
                    target_actor=vector(old_guard['position'])),
        'new': dict(camera=vector(probe['camera_position']), actor=vector(event['stopped_positions']['A']),
                    target_actor=[body[0], body[1], body[2] - 10]),
        'sources': {key: dict(path=name, sha256=hashlib.sha256(raw[key]).hexdigest())
                    for key, name in names.items()},
        'camera_boundary': 'old is the final client camera snapshot, not a per-shot record; new is JSONL record 2',
    }
    return history


def matrix_cases(history):
    return [dict(id=observer + '-' + target, observer=observer, target=target,
                 camera=vector(history[observer]['camera']),
                 positions={'A': vector(history[observer]['actor']),
                            'B': vector(history[target]['target_actor'])})
            for observer in ('old', 'new') for target in ('old', 'new')]


def resource_snapshot(snapshot):
    values = {p['name']: p for p in snapshot['players']}
    return {role: {key: copy.deepcopy(values['EX_' + role][key]) for key in RESOURCE_FIELDS}
            for role in ROLES}


def assert_unchanged(snapshot, baseline):
    assert snapshot.get('phase') == 1 and snapshot.get('map') == MAP, 'Raid/world changed during reconstruction'
    assert snapshot.get('guards') == [], 'Unexpected guard in guards=0 diagnostic'
    assert resource_snapshot(snapshot) == baseline, 'Health, ammunition, shot counters or proxy identity changed'


def assert_fresh_profiles(snapshot, expected_profiles=None):
    """stored_results enumerates all created profiles, including unsettled ones."""
    rows = snapshot.get('stored_results')
    assert isinstance(rows, list) and len(rows) <= 2, 'Expected only the two isolated account profiles'
    profiles = [row.get('profile') for row in rows]
    assert all(isinstance(p, str) and p and p != '0' * 32 for p in profiles), 'Missing bound profile identity'
    assert len(set(profiles)) == len(profiles), 'Duplicate stored profile identity'
    for row in rows:
        assert row.get('outcome') == 0, 'Profile already has a settled outcome'
        assert row.get('progression', {}).get('experience') == 0, 'Profile has earned experience'
        assert row.get('economy_receipts') == 0, 'Profile has previous economy receipts'
    if expected_profiles is not None:
        assert len(expected_profiles) == 2 and len(set(expected_profiles)) == 2
        assert set(profiles) == set(expected_profiles), 'Stored profiles do not match the two bound accounts'
    return copy.deepcopy(rows)

def fixture_players(snapshot, positions):
    values = {p['name']: p for p in snapshot['players']}
    return [dict(name='EX_' + role, position=vector(positions[role]),
                 health=values['EX_' + role]['health'], shield=values['EX_' + role]['shield'])
            for role in ROLES]


def position_observation(snapshot, positions):
    # PlayerState may already exist while its remote pawn is not yet relevant.
    # Absence of pawn-derived fields is readiness, never an invented coordinate.
    values = {p['name']: p for p in snapshot.get('players', [])}
    observed = {}
    for role in ROLES:
        row = values.get('EX_' + role)
        missing = ['player'] if row is None else [key for key in ('position', 'falling', 'pawn_name')
                   if row.get(key) is None or (key == 'pawn_name' and not row[key])]
        value = dict(ready=not missing, missing_fields=missing, requested=positions[role],
                     pawn_name=row.get('pawn_name') if row else None,
                     actual=None, offset_cm=None, distance_cm=None, falling=None)
        if not missing:
            actual = vector(row['position'])
            assert isinstance(row['falling'], bool), 'Invalid replicated falling state'
            value.update(actual=actual, offset_cm=[a-b for a, b in zip(actual, positions[role])],
                         distance_cm=math.dist(actual, positions[role]), falling=row['falling'])
        observed[role] = value
    return observed


def positions_match(observation):
    return set(observation) == set(ROLES) and all(
        value.get('ready') is True and not value['falling']
        and value['distance_cm'] <= POSITION_TOLERANCE_CM for value in observation.values())


def matching_probe(record, case, shooter_actor, target_actor, after_time):
    if record.get('role') != 'A' or record.get('target') != 'EX_B' or record.get('world_time', -1) <= after_time:
        return False
    p = record.get('aim_diagnostics', {})
    if (p.get('read_only_probe') is not True or p.get('target_actor') != target_actor
            or p.get('shooter_actor') != shooter_actor or p.get('target_name') != 'EX_B'
            or p.get('configured_head') is not False or p.get('configured_bone') != 'None'
            or p.get('configured_body_center') is not False or p.get('muzzle_probe_available') is not True):
        return False
    if math.dist(vector(p['camera_position']), case['camera']) > CAMERA_TOLERANCE_CM:
        return False
    target_body = list(case['positions']['B'])
    target_body[2] += 10
    for name in ('body', 'selected'):
        point = p.get('points', {}).get(name, {})
        if point.get('available') is not True or math.dist(vector(point['position']), target_body) > POSITION_TOLERANCE_CM:
            return False
    direction = vector(p['control_direction'])
    delta = [b-a for a, b in zip(p['camera_position'], p['points']['selected']['position'])]
    denominator = math.sqrt(sum(x*x for x in direction) * sum(x*x for x in delta))
    if denominator == 0 or sum(a*b for a, b in zip(direction, delta)) / denominator < .9999:
        return False
    for name in ('body', 'head', 'spine2', 'selected'):
        point = p['points'].get(name, {})
        if point.get('available') is not True:
            return False
        for query in QUERIES:
            hit = point.get(query, {})
            if not all(key in hit for key in ('blocking', 'actor', 'component', 'component_path', 'bone',
                                              'location', 'impact_point', 'impact_normal', 'start_penetrating', 'distance')):
                return False
    return True


def run(g):
    send, wait, state, player = (g[k] for k in ('send', 'wait', 'state', 'player'))
    root, out, args = g['ROOT'], g['OUT'], g['args']
    assert g['ROLES'] == ['A', 'B'] and set(g['processes']) == {'Server', 'A', 'B'}
    assert args.guards == 0 and args.flight_diagnostics, 'Explicit --guards 0 --flight-diagnostics required'
    assert 'A' in g['rendered_roles'], 'Render A for the fixed diagnostic camera'
    assert not (out / 'FlightMatrixEvidence.json').exists(), 'Never overwrite a prior matrix run'
    for role in ('Server', 'A', 'B'):
        commands = g['report']['processes'][role]['command']
        store = next(c.split('=', 1)[1] for c in commands if c.startswith('-ExtractionStore='))
        assert Path(store).resolve() == (out / 'Store').resolve(), 'Dedicated fresh-run store required'
        if role != 'Server':
            assert '-ExtractionProfile=' + args.tag + '_' + role in commands, 'Dedicated account alias required'
    initial_profiles = assert_fresh_profiles(state('Server'))
    history = load_history(root)
    proof = g['report']['flight_matrix'] = dict(
        result='running', main_route_acceptance=False, history=history, cases=[], commands=[],
        initial_stored_profiles=initial_profiles,
        scope='Fixture setup and same-class player proxy reconstruction; queries alone are read-only. No historical pose or live guard replay.',
        boundaries=['No Fire, health change, ammunition change or AI command.',
                    'Legacy fixture requires health/shield setters with unchanged current values.',
                    'B is a player proxy of the same pawn class, not the old guard; idle bones and muzzle are reconstructed.',
                    'Fixed world_camera is a test view, not the historical first-person animation or shot frame.',
                    'Blocker identity is observed, not asserted; completed capture is not a combat or route pass.'],
        tolerances_cm=dict(actor=POSITION_TOLERANCE_CM, camera=CAMERA_TOLERANCE_CM),
        samples_per_case=SAMPLES_PER_CASE)
    g['report'].setdefault('input_sha256', {})['Scripts/Extraction/flight_matrix_scenarios.py'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    evidence_path = out / 'FlightMatrixEvidence.json'
    raw_path = out / 'A-aim-diagnostics.jsonl'
    baseline = None
    raw_cache = ''

    def save():
        evidence_path.write_text(json.dumps(proof, indent=2, ensure_ascii=True) + '\n', encoding='utf-8')
        g['save']()

    def command(role, op, **fields):
        assert op in ('storage', 'ready', 'walk', 'inventory', 'aim', 'camera', 'world_camera', 'fixture', 'pose'), op
        proof['commands'].append(dict(role=role, op=op, fields=copy.deepcopy(fields)))
        error = send(role, op, **fields)
        assert not error, (role, op, error)

    def safe():
        assert_unchanged(state('Server'), baseline)

    def read_raw():
        nonlocal raw_cache
        try:
            raw_cache = g['read_snapshot'](raw_path)
        except OSError:
            # A short append may hold the Windows file handle; retry next poll.
            proof['raw_read_retries'] = proof.get('raw_read_retries', 0) + 1
        return raw_cache

    save()
    try:
        for role in ROLES:
            wait(lambda r=role: state(r).get('map') == '/Game/Maps/Lobby'
                 and state(r).get('account', {}).get('bound')
                 and not state(r)['account'].get('locked'), 'fresh lobby account ' + role, 90)
            account = state(role)['account']
            assert account['outcome'] == 0 and account['progression']['experience'] == 0
            gun = next((i for i in account['loadout']['items'] if i['slot'] == 0), None)
            if gun is None:
                gun = next(i for i in account['stash']['items'] if i['definition'] == 'AK')
                revision = account['revision']
                command(role, 'storage', item=gun['id'], quantity=1, slot=0)
                wait(lambda r=role, v=revision: state(r)['account']['revision'] > v, 'normal AK loadout ' + role)
            equipped = next(i for i in state(role)['account']['loadout']['items'] if i['slot'] == 0)
            assert equipped['definition'] == 'AK' and equipped['quantity'] == 1
        proof['account_profiles'] = {r: state(r)['account']['profile'] for r in ROLES}
        assert len(set(proof['account_profiles'].values())) == 2
        wait(lambda: {row.get('profile') for row in state('Server').get('stored_results', [])}
             == set(proof['account_profiles'].values()), 'both bound profiles recorded by authority')
        proof['verified_stored_profiles'] = assert_fresh_profiles(
            state('Server'), list(proof['account_profiles'].values()))
        for role in ROLES:
            command(role, 'ready')
        wait(lambda: all(state(r).get('phase') == 1 for r in ('Server', 'A', 'B'))
             and all(player('Server', r).get('mechanics_ready') and player(r, r).get('mechanics_ready')
                     and not player('Server', r).get('falling', True) for r in ROLES),
             'ordinary guarded-free spawn with equipped AK', 180)
        baseline = resource_snapshot(state('Server'))
        assert baseline['A']['pawn_class'] == baseline['B']['pawn_class']
        assert baseline['A']['pawn_class'].endswith('BP_StingerCharacter.BP_StingerCharacter_C')
        for value in baseline.values():
            assert value['health'] > 0 and value['life'] == 0 and not value['bleeding'] and not value['fracture']
            assert value['accepted'] == value['rejected'] == value['bullet_sequence'] == value['guard_kills'] == 0
        safe()
        proof['ordinary_spawn'] = {r: copy.deepcopy(player('Server', r)) for r in ROLES}
        proof['unchanged_resource_baseline'] = baseline
        for role in ROLES:
            command(role, 'walk', points=[])
            command(role, 'inventory', open=False)
            command(role, 'aim', target='', head=False)
        for case in matrix_cases(history):
            event = copy.deepcopy(case) | dict(result='running', samples=[])
            proof['cases'].append(event)
            command('A', 'camera', target='')
            command('A', 'aim', target='', head=False)
            safe()
            placement = fixture_players(state('Server'), case['positions'])
            event['fixture_payload_preserving_health_and_shield'] = placement
            command('Server', 'fixture', players=placement)

            pending_seen = set()

            def positioned():
                safe()
                snapshots = {peer: state(peer) for peer in ('Server', 'A', 'B')}
                event['observed_positions'] = {peer: position_observation(snapshot, case['positions'])
                                               for peer, snapshot in snapshots.items()}
                for peer, observations in event['observed_positions'].items():
                    for role, observation in observations.items():
                        signature = (peer, role, tuple(observation['missing_fields']))
                        if not observation['ready'] and signature not in pending_seen:
                            pending_seen.add(signature)
                            event.setdefault('replica_pending_observations', []).append(dict(
                                peer=peer, role=role, world_time=snapshots[peer].get('world_time'),
                                observation=copy.deepcopy(observation)))
                return all(positions_match(obs) for obs in event['observed_positions'].values())

            wait(positioned, 'replicated grounded matrix coordinates ' + case['id'], 15)
            center = list(case['positions']['B'])
            center[2] += 10
            command('A', 'world_camera', position=case['camera'], target=center, field_of_view=90)
            command('A', 'aim', target='EX_B', head=False)
            after = state('A')['world_time']
            event['sample_after_client_world_time'] = after
            event['world_camera_command'] = dict(position=case['camera'], target=center, field_of_view=90)
            seen = set()

            def sampled():
                if not positioned():
                    return False
                for number, record in enumerate(read_complete_records(read_raw()), 1):
                    observed = event['observed_positions']['A']
                    if number in seen or not matching_probe(record, case, observed['A']['pawn_name'],
                                                           observed['B']['pawn_name'], after):
                        continue
                    seen.add(number)
                    event['samples'].append(dict(source_line=number, raw=copy.deepcopy(record),
                                                  observed_positions=copy.deepcopy(event['observed_positions'])))
                if len(event['samples']) < SAMPLES_PER_CASE:
                    return False
                times = [sample['raw']['world_time'] for sample in event['samples']]
                return max(times) - min(times) >= 1.0

            wait(sampled, 'three fresh fixed-camera probe samples ' + case['id'], 15)
            command('A', 'pose', target='EX_B', name='flight-matrix-' + case['id'] + '-proxy')
            event['proxy_pose_file'] = 'A-flight-matrix-' + case['id'] + '-proxy-pose.json'
            pose = out / event['proxy_pose_file']
            assert pose.is_file(), 'Driver did not persist proxy pose'
            event['proxy_pose_sha256'] = hashlib.sha256(pose.read_bytes()).hexdigest()
            event['result'] = 'captured'
            save()
        safe()
        proof['result'] = 'completed_diagnostic_capture'
    except BaseException as error:
        proof['result'] = 'failed'
        proof['error'] = repr(error)
        raise
    finally:
        for op, fields in [('aim', dict(target='', head=False)), ('camera', dict(target=''))]:
            try:
                command('A', op, **fields)
            except Exception as error:
                proof.setdefault('cleanup_errors', []).append(repr(error))
        completion_error = None
        if proof['result'] == 'completed_diagnostic_capture':
            try:
                safe()
                assert not proof.get('cleanup_errors'), 'Diagnostic camera cleanup failed'
            except Exception as error:
                completion_error = error
                proof['result'] = 'failed'
                proof['error'] = repr(error)
        # Aim has been cleared and acknowledged; preserve the raw file bytes,
        # including original line endings, rather than reserializing records.
        raw_bytes = raw_path.read_bytes() if raw_path.exists() else b''
        copy_path = out / 'FlightMatrixRawProbes.jsonl'
        copy_path.write_bytes(raw_bytes)
        text = raw_bytes.decode('utf-8-sig')
        proof['all_raw_probes'] = dict(file=copy_path.name, source=raw_path.name,
                                     complete_records=len(read_complete_records(text)),
                                     trailing_partial_bytes=len(raw_bytes) - (raw_bytes.rfind(b'\n') + 1),
                                     sha256=hashlib.sha256(raw_bytes).hexdigest())
        save()
        if completion_error is not None:
            raise completion_error
