"""Normal lobby -> measured walking/search/combat -> timed exit -> next raid.

Invoked by run_network.py --scenario overnight-route. Requires the development
driver's read-only exits/guard route observations and server-only fixed spawn.
No fixture, teleport, inventory injection, forced outcome or time skip is used.
The first raid is complete; the second is prepared and entered, not claimed as
a second completed extraction. Use fresh isolated test accounts.
"""
import hashlib
import json
import math
import time
import uuid
from pathlib import Path

from exit_feedback_scenarios import complete_png


def read_route(root):
    from overnight_route_plan import validate
    file = root / 'Scripts/Extraction/overnight_route.json'
    route = json.loads(file.read_text(encoding='utf-8'))
    problems = validate(route)
    assert route['status'] == 'graph_connected_requires_runtime_validation' and not problems, problems
    with (root / route['map']).open('rb') as stream:
        current = hashlib.file_digest(stream, 'sha256').hexdigest()
    assert current == route['planned_map_sha256'], 'Map changed since route planning; remeasure affected floors and rebuild route'
    return route


def owned_items(account):
    return [item for side in ('stash', 'loadout', 'overflow')
            for item in account.get(side, {}).get('items', [])]


def item_owners(snapshot, item_id):
    """Read each live authority container once, excluding result/journal copies."""
    owners = []

    def inspect(owner, items):
        for item in items:
            if item.get('id') == item_id:
                owners.append(dict(owner=owner, quantity=item.get('quantity'),
                                   definition=item.get('definition'), slot=item.get('slot')))
            inspect(owner + ':attachment', item.get('attachments', []))

    for participant in snapshot.get('players', []):
        prefix = 'player:' + participant['name']
        inspect(prefix + ':inventory', participant.get('inventory', {}).get('items', []))
        for side in ('stash', 'loadout', 'overflow'):
            inspect(prefix + ':' + side, participant.get('account', {}).get(side, {}).get('items', []))
    for box in snapshot.get('loot', []):
        inspect('loot:' + box['id'], box.get('contents', {}).get('items', []))
    for guard in snapshot.get('guards', []):
        inspect('guard:' + guard['name'], guard.get('inventory', {}).get('items', []))
    return owners


def ownership_is(snapshot, item_id, owner, quantity, slot=None):
    matches = item_owners(snapshot, item_id)
    return (len(matches) == 1 and matches[0]['owner'] == owner
            and matches[0]['quantity'] == quantity
            and (slot is None or matches[0]['slot'] == slot))


def sale_is_complete(snapshot, role, item_id, before_credits, quote, before_revision):
    assert isinstance(quote, (int, float)) and quote > 0, 'Sale requires a positive production quote'
    account = next(p['account'] for p in snapshot['players'] if p['name'] == 'EX_' + role)
    return (account['revision'] > before_revision
            and account['progression']['credits'] == before_credits + quote
            and not item_owners(snapshot, item_id))



def preparation_ammunition(player_state, weapon_id):
    """Capture idle, owned weapon ammunition without assuming a full magazine."""
    assert player_state['weapon_item'] == weapon_id, 'Preparation substituted the original weapon'
    weapons = [i for i in player_state['inventory']['items'] if i['id'] == weapon_id]
    assert len(weapons) == 1 and weapons[0]['slot'] == 0 and weapons[0]['quantity'] == 1, (
        'Preparation requires the one original primary weapon')
    weapon = weapons[0]
    ammo, reserve = player_state['ammo'], player_state['carried_ammo']
    assert player_state['combat_state'] == 0, 'Preparation weapon is not idle'
    assert ammo >= 0 and reserve >= 0 and ammo == weapon['magazine'], 'Weapon and stored magazine disagree'
    assert reserve == sum(i['quantity'] for i in player_state['inventory']['items']
                          if i['definition'] == weapon['ammo_type']), 'Reserve is not backed by owned ammunition'
    return dict(weapon=weapon_id, ammo_type=weapon['ammo_type'], ammo=ammo,
                carried_ammo=reserve, accepted=player_state['accepted'])


def validate_preparation_reload(before, after):
    assert before['ammo'] == 0 and before['carried_ammo'] > 0, 'Empty original weapon has no reserve ammunition'
    assert after['weapon'] == before['weapon'] and after['ammo_type'] == before['ammo_type'], (
        'Preparation reload changed weapon identity or ammunition type')
    assert after['ammo'] > 0 and after['carried_ammo'] >= 0, 'Preparation reload did not load the original weapon'
    assert after['accepted'] == before['accepted'], 'Unexpected firing during preparation reload'
    assert after['ammo'] + after['carried_ammo'] == before['ammo'] + before['carried_ammo'], (
        'Preparation reload did not conserve owned ammunition')


def authoritative_raid(player_state):
    value = player_state.get('extraction_raid')
    assert isinstance(value, str), 'Missing authoritative extraction_raid'
    try:
        ident = uuid.UUID(value)
    except ValueError as exc:
        raise AssertionError('Invalid authoritative extraction_raid') from exc
    assert ident.int != 0, 'Zero authoritative extraction_raid'
    return str(ident)


def assert_closed_square(player_state, outcome, exit_id):
    assert player_state.get('life') == 0, 'Closed exit participant is not alive'
    assert player_state.get('extract_at') == 0, 'Closed exit started hold progress early'
    assert player_state.get('extract_exit_id') == exit_id, 'Closed square condition not identified'
    assert player_state.get('extract_blocked_reason'), 'Closed square is missing its condition feedback'
    assert outcome != 1, 'Closed exit settled early'


def hold_deadline(player_state, exit_id, opens_at, hold_seconds):
    assert hold_seconds == 10, 'Square production hold must remain exactly 10 seconds'
    assert player_state.get('extract_exit_id') == exit_id, 'Wrong exit owns the hold timer'
    deadline = player_state.get('extract_at', 0)
    assert deadline >= opens_at + hold_seconds - .01, 'Hold timer is shorter than production rule'
    return deadline


def square_is_open(read_snapshot, roles, exit_id, opening):
    # run_network.state() reopens the file on every call. Read exactly once so
    # the time before opening cannot be paired with a player's later hold timer.
    snapshot = read_snapshot()
    players = {p['name']: p for p in snapshot['players']}
    now = snapshot['world_time']
    for role in roles:
        p = players['EX_' + role]
        assert p.get('life') == 0 and p.get('health', 0) > 0, (role, 'Exit participant is no longer alive')
        if now < opening:
            assert_closed_square(p, p['account'].get('outcome'), exit_id)
    return now >= opening and any(e['id'] == exit_id and e['available'] for e in snapshot.get('exits', []))


def settled_after_deadlines(read_snapshot, roles, exit_id, holds):
    snapshot = read_snapshot()
    players = {p['name']: p for p in snapshot['players']}
    now = snapshot['world_time']
    for role in roles:
        p = players['EX_' + role]
        outcome = p['account'].get('outcome')
        if now < holds[role] - .01:
            assert p.get('life') == 0 and outcome != 1, (role, 'Settled before the required hold deadline')
            assert p.get('extract_exit_id') == exit_id and abs(p.get('extract_at', 0)-holds[role]) < .01, (
                role, 'Hold changed during continuous stationary extraction')
    return all(players['EX_' + role]['account'].get('outcome') == 1 for role in roles)


def path_chunk(points, completed, size=4):
    assert isinstance(completed, int) and 0 <= completed <= len(points)
    assert 1 <= size <= 4
    if completed == len(points):
        return None
    end = min(len(points), completed + size)
    return dict(start=completed, end=end, points=points[completed:end])


def at_ground_point(player_state, point):
    return (not player_state.get('falling', True)
            and math.dist(player_state['position'][:2], point[:2]) < 65
            and abs(player_state['position'][2] - player_state['capsule_half'] - point[2]) < 90)


def complete_path_chunk(chunk, completed, movement, player_state):
    """Only an acknowledged, grounded original slice advances the global cursor."""
    assert chunk['start'] == completed and chunk['end'] > completed, 'Path resume skipped or replayed an original slice'
    count = chunk['end'] - chunk['start']
    assert count == len(chunk['points']) and movement.get('walk_count') == count and movement.get('walk_index') == count, (
        'Driver has not completed this exact original path slice')
    assert at_ground_point(player_state, chunk['points'][-1]), 'Chunk endpoint lacks authoritative ground evidence'
    return chunk['end']


# Exactly the same measured bridge nodes used by both players, independent of
# their different global indices. All 30 source probes name the same floor
# actor SCR_Group_11c34bd23940006f. No rectangle authorizes another floor/path.
BRIDGE_POINTS = [
    [2200., -18600., 189.2753], [2300., -18600., 240.7015],
    [2400., -18600., 274.5848], [2500., -18600., 326.1215],
    [2600., -18600., 359.966], [2700., -18600., 395.0148],
    [2800., -18600., 395.0148], [2900., -18600., 395.0198],
    [3000., -18600., 395.0478], [3100., -18600., 411.1523],
    [3200., -18600., 443.6607], [3300., -18600., 493.2117],
    [3300., -18500., 510.3187], [3400., -18500., 543.0012],
    [3500., -18500., 592.7188], [3600., -18500., 625.346],
    [3700., -18500., 675.3441], [3800., -18500., 707.941],
    [3800., -18400., 724.7417], [3900., -18400., 773.9978],
    [4000., -18400., 807.1825], [4000., -18300., 823.824],
    [4000., -18200., 840.4602], [4100., -18200., 873.117],
    [4200., -18200., 907.0364], [4300., -18200., 907.0364],
    [4400., -18200., 907.0364], [4500., -18200., 907.0364],
    [4600., -18200., 907.0364], [4600., -18100., 907.3128],
]


def bridge_index(point):
    return next((index for index, measured in enumerate(BRIDGE_POINTS)
                 if math.dist(point, measured) < 1), None)


def signed_edge_fraction(position, left, right):
    """Progress only: preserve travel before/after the measured edge endpoints."""
    dx, dy = right[0] - left[0], right[1] - left[1]
    return ((position[0] - left[0]) * dx + (position[1] - left[1]) * dy) / (dx*dx + dy*dy)


def project_edge(position, left, right):
    # Eligibility/ground distance remains distance to the finite segment.
    t = max(0., min(1., signed_edge_fraction(position, left, right)))
    ground = [left[i] + t * (right[i] - left[i]) for i in range(3)]
    return t, math.dist(position[:2], ground[:2]), ground[2]


class LowLedgeJump:
    """Bounded normal Jump recovery on the measured bridge, never a fixture.

    History is shared by the role across chunks. Another attempt requires a
    completed landing and real forward progress; repeating a failed stationary
    jump is forbidden even if the driver changes its local target index.
    """
    MAX_ATTEMPTS = 36
    MAX_SECONDS = 180

    def __init__(self, chunk, history=None):
        self.chunk = chunk
        self.history = history if history is not None else []
        self.events = []
        self.last_time = None
        self.anchor = None
        self.movement_target = None
        self.movement_start = None
        self.event = None

    def target(self, movement):
        index = movement.get('walk_index', -1)
        points = self.chunk['points']
        if (movement.get('error') or movement.get('walk_count') != len(points)
                or not isinstance(index, int) or not 0 <= index < len(points)):
            return None
        measured = bridge_index(points[index])
        return measured if measured is not None and measured > 0 else None

    def observe_movement_start(self, now, movement, participant):
        target = self.target(movement)
        if target != self.movement_target:
            self.movement_target, self.movement_start = target, None
        if target is None or self.movement_start is not None or participant.get('falling') is not False:
            return
        previous = BRIDGE_POINTS[target-1]
        position = participant['position']
        feet = position[2] - participant['capsule_half']
        # A driver target changes when its previous point is within 45 cm.
        # Capture that real grounded start once; never slide/recenter this
        # eligibility corridor along with later collision deflection.
        if math.dist(position[:2], previous[:2]) < 45 and abs(feet - previous[2]) < 45:
            self.movement_start = dict(world_time=now, position=list(position), feet_z=feet,
                previous_measured_point=list(previous), target=list(BRIDGE_POINTS[target]))

    def movement_scope(self, movement, participant):
        target = self.target(movement)
        if target is None:
            return None
        position = participant['position']
        _, distance, ground = project_edge(position, BRIDGE_POINTS[target-1], BRIDGE_POINTS[target])
        feet = position[2] - participant['capsule_half']
        actual_distance = None
        if self.movement_target == target and self.movement_start is not None:
            _, actual_distance, _ = project_edge(position, self.movement_start['position'], BRIDGE_POINTS[target])
        return dict(measured_edge_distance_cm=distance, observed_input_edge_distance_cm=actual_distance,
                    measured_floor_delta_cm=feet-ground, observed_start=self.movement_start,
                    allowed=abs(feet-ground) < 45 and (distance < 65 or actual_distance is not None and actual_distance < 65))

    def eligible(self, movement, participant):
        target = self.target(movement)
        scope = self.movement_scope(movement, participant)
        position = participant['position']
        return (scope is not None and scope['allowed']
                and math.dist(position[:2], BRIDGE_POINTS[target][:2]) > 45
                and participant.get('life') == 0 and participant.get('health', 0) > 0
                and participant.get('falling') is False and participant.get('traversal') == 0
                and all(participant.get(key) is False for key in
                        ('disabled', 'crouched', 'inventory_open', 'menu_open')))

    def check_budget(self, now):
        if self.history:
            assert now - self.history[0]['trigger']['world_time'] <= self.MAX_SECONDS, (
                'Bridge recovery exceeded the role 180 second budget')

    def should_jump(self, now, movement, participant):
        if self.target(movement) is not None:
            self.check_budget(now)
        if self.last_time is not None and now <= self.last_time:
            return False
        self.last_time = now
        self.observe_movement_start(now, movement, participant)
        if (not self.eligible(movement, participant)
                or self.history and self.history[-1]['landing'] is None):
            self.anchor = None
            return False
        position = list(participant['position'])
        original_index = self.chunk['start'] + movement['walk_index']
        if (self.anchor is None or self.anchor['original_index'] != original_index
                or math.dist(position, self.anchor['position']) > 5):
            self.anchor = dict(world_time=now, position=position, original_index=original_index)
            return False
        if now - self.anchor['world_time'] < 2:
            return False
        target = self.target(movement)
        fraction, _, _ = project_edge(position, BRIDGE_POINTS[target-1], BRIDGE_POINTS[target])
        # Every authored edge here is exactly 100 cm in XY, including turns.
        bridge_progress = 100 * (target - 1 + fraction)
        signed_progress = 100 * (target - 1 + signed_edge_fraction(
            position, BRIDGE_POINTS[target-1], BRIDGE_POINTS[target]))
        progress_evidence = dict(kind='first bridge recovery')
        if self.history:
            previous = self.history[-1]
            landing = previous['landing']
            assert (previous['falling'] is not None and landing is not None
                    and previous['falling']['world_time'] < landing['world_time'] <= now), (
                'Previous bridge jump lacks authoritative falling and landing')
            xy_progress = math.dist(position[:2], previous['trigger']['position'][:2])
            old_target = previous['target']
            previous_bridge_index = bridge_index(old_target)
            same_target = (original_index == previous['original_global_index'] and target == previous_bridge_index)
            clamped_progress = bridge_progress - previous['trigger']['bridge_progress_cm']
            # At route08's same target, the previous real start was 21 cm
            # behind t=0. Clamping erased that travel. Only compare signed
            # parameters on the identical measured edge; cross-target turns
            # retain the existing original-point/landing evidence below.
            if same_target:
                previous_signed = 100 * (target - 1 + signed_edge_fraction(
                    previous['trigger']['position'], BRIDGE_POINTS[target-1], BRIDGE_POINTS[target]))
                projected_progress = signed_progress - previous_signed
            else:
                projected_progress = clamped_progress
            # A 90-degree grid turn can reduce the projection delta even when
            # the player really completed the previous original point. Require
            # both the recorded landing and current ground position inside its
            # unchanged driver 45 cm arrival radius, not merely an advanced cursor.
            completed_original_point = (
                original_index == previous['original_global_index'] + 1
                and previous_bridge_index is not None and target == previous_bridge_index + 1
                and participant.get('falling') is False
                and math.dist(landing['position'][:2], old_target[:2]) < 45
                and abs(landing['feet_z'] - old_target[2]) < 45
                and math.dist(position[:2], old_target[:2]) < 45
                and abs(position[2] - participant['capsule_half'] - old_target[2]) < 45)
            assert xy_progress >= 25 and (projected_progress >= 25 or completed_original_point), (
                'Bridge recovery refuses to repeat a jump at the same unadvanced obstruction')
            progress_evidence = dict(kind=('signed progress on same measured edge' if same_target
                                    else 'measured polyline progress') if projected_progress >= 25
                                    else 'observed completed original point at turn',
                actual_xy_progress_cm=xy_progress, projected_progress_cm=projected_progress,
                clamped_progress_cm=clamped_progress, same_original_target=same_target,
                completed_original_point=completed_original_point, previous_target=old_target,
                previous_global_index=previous['original_global_index'], previous_landing=landing,
                current_world_time=now, current_position=position)
        assert len(self.history) < self.MAX_ATTEMPTS, 'Bridge recovery exceeded the role 36 Jump limit'
        index = movement['walk_index']
        self.event = dict(result='requested', source_run='overnight-route06',
                          source_log='A.log:1356-1389',
                          source_log_sha256='2bfddece12c77016820441aed2584e4077377bfa55060ea418ade59f44463663',
                          attempt=len(self.history) + 1, scope='30 measured bridge polyline points',
                          progress_evidence=progress_evidence,
                          original_start=self.chunk['start'], original_end=self.chunk['end'],
                          original_local_index=index, original_global_index=self.chunk['start'] + index,
                          target=list(self.chunk['points'][index]), trigger=dict(world_time=now, position=position,
                          feet_z=position[2] - participant['capsule_half'], bridge_progress_cm=bridge_progress,
                          signed_bridge_progress_cm=signed_progress,
                          stationary_since=self.anchor['world_time'], movement_scope=self.movement_scope(movement, participant)),
                          input=dict(action='Jump', duration=.08), falling=None, landing=None,
                          original_completion=None, return_segments=[])
        self.history.append(self.event)
        self.events.append(self.event)
        self.anchor = None
        return True

    def observe_air(self, now, participant):
        assert self.event is not None, 'Cannot observe recovery before requesting normal Jump'
        self.check_budget(now)
        event = self.event
        if event['landing'] is not None or now <= event['trigger']['world_time']:
            return
        assert event['falling'] is not None or now - event['trigger']['world_time'] <= 3, (
            'Normal Jump did not produce observed authoritative falling within 3 seconds at the measured bridge')
        observation = dict(world_time=now, position=list(participant['position']),
                           feet_z=participant['position'][2] - participant['capsule_half'])
        if participant.get('falling') is True:
            if event['falling'] is None:
                event['falling'] = observation
            event['last_falling'] = observation
        elif participant.get('falling') is False and event['falling'] is not None:
            if now > event['falling']['world_time'] and event['landing'] is None:
                event['landing'] = observation
                event['result'] = 'landed'


def ledge_return_points(points, chunk, participant):
    """Return from grounded inertia runout along the existing measured edge chain.

    Future nodes are only recovery waypoints; they never advance the original
    global cursor. Every bend is retained; a landing off this bridge fails.
    """
    assert participant.get('falling') is False, 'Cannot plan a return while airborne'
    endpoint = chunk['points'][-1]
    if at_ground_point(participant, endpoint):
        return []
    feet = participant['position'][2] - participant['capsule_half']
    candidates = []
    for index in range(max(0, chunk['start'] - 1), min(len(points) - 1, chunk['end'] + 8)):
        left, right = points[index:index+2]
        a, b = bridge_index(left), bridge_index(right)
        if a is None or b != a + 1:
            continue
        _, distance, ground_z = project_edge(participant['position'], left, right)
        if distance < 65 and abs(feet - ground_z) < 90:
            candidates.append((distance, index))
    assert candidates, ('Jump landing left the original measured bridge corridor/layer', participant['position'])
    _, index = min(candidates)
    endpoint_index = chunk['end'] - 1
    if index < endpoint_index:
        result = points[index + 1:endpoint_index + 1]
    else:
        result = list(reversed(points[endpoint_index:index + 1]))
    assert result and result[-1] == endpoint, 'Measured return did not retain the original endpoint'
    return result


def encounter_trigger(snapshot, route_name, roles=('A', 'B')):
    guards = [g for g in snapshot.get('guards', []) if g.get('route') == route_name and not g.get('dead')]
    assert len(guards) <= 1, 'Expected one authored dorm guard'
    if not guards:
        return None
    guard = guards[0]
    players = {p['name']: p for p in snapshot['players']}
    for role in roles:
        p = players['EX_' + role]
        # Target is the authority Service's currently perceived, visible pawn.
        # Mere distance, an Engage enum or a geometric LOS ray is insufficient.
        if p.get('life') == 0 and p.get('pawn_name') and guard.get('target') == p['pawn_name']:
            return dict(shooter=role, guard=guard['name'], route=route_name,
                        world_time=snapshot['world_time'], target=guard['target'],
                        guard_state=guard.get('state'), guard_health=guard['health'], guard_shots=guard['shots'],
                        player_health={r: players['EX_' + r]['health'] for r in roles}, reason='actual visible target')
    return None


def validate_encounter(before, after, shooter, guard_name):
    first = next(g for g in before['guards'] if g['name'] == guard_name)
    last = next(g for g in after['guards'] if g['name'] == guard_name)
    a = next(p for p in before['players'] if p['name'] == 'EX_' + shooter)
    b = next(p for p in after['players'] if p['name'] == 'EX_' + shooter)
    assert not first['dead'] and first['health'] > 0, 'An already defeated guard is not combat evidence'
    assert first['route'] == last['route'] and last['dead'] and last['health'] < first['health'], (
        'The same live guard must actually take damage and be defeated')
    assert b['accepted'] > a['accepted'], 'No real player shots were accepted'
    assert b['guard_kills'] == a['guard_kills'] + 1, 'Guard death was not credited to this shooter'
    return dict(guard=guard_name, route=last['route'], shooter=shooter,
                accepted_shots=b['accepted']-a['accepted'], accepted_before=a['accepted'], accepted_after=b['accepted'],
                guard_health_before=first['health'], guard_health_after=last['health'],
                guard_dead_before=first['dead'], guard_dead_after=last['dead'],
                guard_kills_before=a['guard_kills'], guard_kills_after=b['guard_kills'],
                kills_added=b['guard_kills']-a['guard_kills'])


def accepted_without_damage(enabled, before, current, shooter, guard_name):
    """Opt-in failure acceleration, never a combat success condition."""
    if enabled is not True:
        return False
    first = next(g for g in before['guards'] if g['name'] == guard_name)
    guard = next(g for g in current['guards'] if g['name'] == guard_name)
    previous = next(p for p in before['players'] if p['name'] == 'EX_' + shooter)
    owner = next(p for p in current['players'] if p['name'] == 'EX_' + shooter)
    return (not first['dead'] and not guard['dead'] and guard['route'] == first['route']
            and guard['health'] == first['health'] and owner['accepted'] - previous['accepted'] >= 6)


def confirm_completed_guard(snapshot, route_name, proof):
    """A reclaimed corpse is acceptable only after the same recorded live fight."""
    expected = proof.get('authored_guard', {})
    combat = proof.get('combat', {})
    assert expected.get('route') == route_name and expected.get('name'), 'Missing initial authored guard identity'
    assert combat.get('guard') == expected['name'] and combat.get('route') == route_name, (
        'Completed combat does not belong to the original authored guard')
    index = combat.get('encounter_index')
    encounters = proof.get('encounters', [])
    assert isinstance(index, int) and 0 <= index < len(encounters), 'Missing completed encounter evidence'
    event = encounters[index]
    assert event.get('result') == 'passed', 'Encounter was not verified before the corpse disappeared'
    assert event.get('trigger', {}).get('guard') == expected['name'], 'Encounter trigger identifies another guard'
    assert event.get('trigger', {}).get('route') == route_name, 'Encounter trigger identifies another route'
    fields = ('guard', 'route', 'shooter', 'accepted_shots', 'accepted_before', 'accepted_after',
              'guard_health_before', 'guard_health_after', 'guard_dead_before', 'guard_dead_after',
              'guard_kills_before', 'guard_kills_after', 'kills_added')
    assert all(key in event and event[key] == combat.get(key) for key in fields), 'Combat summary differs from its encounter'
    assert event['guard_dead_before'] is False and event['guard_health_before'] > 0, 'Missing live guard evidence'
    assert event['guard_dead_after'] is True and event['guard_health_after'] < event['guard_health_before'], (
        'Missing actual damage and death evidence')
    assert event['accepted_after'] - event['accepted_before'] == event['accepted_shots'] > 0, 'Missing accepted player shots'
    assert event['guard_kills_after'] - event['guard_kills_before'] == event['kills_added'] == 1, 'Missing shooter kill credit'
    owner = next((player for player in snapshot['players'] if player['name'] == 'EX_' + event['shooter']), None)
    assert owner and owner.get('guard_kills', -1) >= event['guard_kills_after'], 'Authority lost the recorded shooter credit'
    assert owner.get('accepted', -1) >= event['accepted_after'], 'Authority lost the recorded accepted shots'
    same_route = [guard for guard in snapshot['guards'] if guard.get('route') == route_name]
    same_actor = [guard for guard in snapshot['guards'] if guard.get('name') == expected['name']]
    assert len(same_route) <= 1 and all(guard['name'] == expected['name'] for guard in same_route), (
        'A different guard replaced the authored encounter')
    assert len(same_actor) <= 1, 'Duplicate authored guard identity'
    if same_actor:
        guard = same_actor[0]
        assert guard.get('route') == route_name and guard.get('dead') is True, 'Recorded defeated guard is alive or on another route'
    return 'present_dead' if same_actor else 'reclaimed_after_verified_defeat'


def run(g):
    send, check, wait, state, player = (g[k] for k in ('send', 'check', 'wait', 'state', 'player'))
    root, out, roles = g['ROOT'], g['OUT'], g['ROLES']
    flight_diagnostics = getattr(g.get('args'), 'flight_diagnostics', False) is True
    assert roles == ['A', 'B'], 'The continuous route uses two same-squad clients'
    assert 'A' in g['rendered_roles'], 'A must render so crosshair shooting is genuine'
    route = read_route(root)
    pair_legs = {(p['a_start'], p['b_start'], p['destination']): p for p in route['pair_legs']}
    proof = dict(result='running', first_raid='pending', second_raid='pending',
                 fixed_spawn_group=route['spawn_group'], search_groups=route['search_groups'],
                 guard_route=route['guard_route'], exit=route['exit'],
                 teleports=0, injected_items=0, skipped_time=0,
                 boundary='Graph planning is separate from the recorded input-driven traversal result.',
                 legs=[], encounters=[], jumps=[], acquired=[], brought_weapons={}, screenshots=[], second_raid_completed=False)
    proof['bridge_recovery_limits'] = dict(max_attempts_per_role=LowLedgeJump.MAX_ATTEMPTS,
        max_server_seconds_per_role=LowLedgeJump.MAX_SECONDS,
        source='overnight-route06 observed jumps and measured bridge heights',
        derivation='About 530 cm remaining ascent / observed minimum 16 cm per jump = 34 jumps rounded up; '
                   'allow 2 additional attempts. 36 * observed upper-stair cycle about 4.9 s = 176.4 s, bounded at 180 s.',
        boundary='Finite test input budget, not a production rule or a guarantee of traversal; real progress, '
                 'falling/landing, original endpoints and all raid checks remain required.')
    bridge_history = {role: [] for role in roles}
    g['report']['overnight_route'] = proof
    for filename in ('overnight_route.json', 'overnight_route_scenarios.py', 'overnight_route_plan.py',
                     'exit_feedback_scenarios.py'):
        file = root / 'Scripts/Extraction' / filename
        g['report'].setdefault('input_sha256', {})[file.relative_to(root).as_posix()] = hashlib.sha256(file.read_bytes()).hexdigest()
    (out / 'OvernightRoute.json').write_bytes((root / 'Scripts/Extraction/overnight_route.json').read_bytes())
    motion = (out / 'OvernightMotion.jsonl').open('w', encoding='utf-8')

    def command(role, op, **fields):
        error = send(role, op, **fields)
        assert not error, (role, op, error)

    def save():
        g['save']()

    def capture(name):
        command('A', 'screenshot', name=name)
        wait(lambda: complete_png(out / ('A-' + name + '.png')), 'complete capture ' + name)
        proof['screenshots'].append('A-' + name + '.png')

    def alive(snapshot=None):
        snapshot = snapshot if snapshot is not None else state('Server')
        players = {p['name']: p for p in snapshot['players']}
        for role in roles:
            p = players['EX_' + role]
            assert p.get('life') == 0 and p.get('health', 0) > 0, (role, 'route participant is no longer alive', p.get('life'))

    def sample(label, snapshot=None, **details):
        snapshot = snapshot if snapshot is not None else state('Server')
        players = {p['name']: p for p in snapshot['players']}
        record = dict(stage=label, monotonic=time.monotonic(), world_time=snapshot.get('world_time'),
                      players={r: {k: players.get('EX_' + r, {}).get(k) for k in
                                  ('position', 'capsule_half', 'health', 'life', 'falling', 'extract_at')}
                               for r in roles},
                      progress={r: [state(r).get('walk_index'), state(r).get('walk_count')] for r in roles}, **details)
        motion.write(json.dumps(record, ensure_ascii=False) + '\n')
        motion.flush()

    def move_storage(role, item, **fields):
        revision = state(role)['account']['revision']
        command(role, 'storage', item=item['id'], quantity=item['quantity'], **fields)
        wait(lambda: state(role)['account']['revision'] > revision, role + ' loadout transaction')

    def start_raid(number):
        for role in roles:
            check(role + ' lobby account unlocked before raid ' + str(number),
                  lambda r=role: state(r).get('map') == '/Game/Maps/Lobby'
                  and state(r).get('account', {}).get('bound') and not state(r)['account'].get('locked'), 90)
            command(role, 'squad', value=1)
        check('Both clients joined the same squad for raid ' + str(number),
              lambda: all(player('Server', r).get('squad') == 1 for r in roles))
        for role in roles:
            account = state(role)['account']
            expected_weapon = proof['brought_weapons'].get(role)
            equipped = next((i for i in account['loadout']['items'] if i['slot'] == 0), None)
            if number == 2:
                assert expected_weapon, 'First raid did not record the original primary weapon'
                assert not equipped, 'Returned primary should be in the stash under the production settlement contract'
                gun = next(i for i in account['stash']['items'] if i['id'] == expected_weapon)
                move_storage(role, gun, slot=0)
            elif not equipped:
                gun = next(i for i in account['stash']['items'] if i['definition'] == 'AK')
                move_storage(role, gun, slot=0)
            equipped = next(i for i in state(role)['account']['loadout']['items'] if i['slot'] == 0)
            assert equipped['definition'] == 'AK' and equipped['quantity'] == 1
            if number == 1:
                proof['brought_weapons'][role] = equipped['id']
            assert equipped['id'] == proof['brought_weapons'][role], 'Preparation substituted the original weapon'
            check(role + ' original weapon has one loadout owner before raid ' + str(number),
                  lambda r=role: ownership_is(state('Server'), proof['brought_weapons'][r], 'player:EX_' + r + ':loadout', 1, 0))
            for definition in ('Ammo', 'Medkit'):
                account = state(role)['account']
                if not any(i['definition'] == definition for i in account['loadout']['items']):
                    item = next(i for i in account['stash']['items'] if i['definition'] == definition)
                    move_storage(role, item, slot=-1)
        if number == 1:
            capture('overnight-lobby-equipped')
        for role in roles:
            command(role, 'ready')
        check('Both clients entered active raid ' + str(number),
              lambda: all(state(r).get('phase') == 1 for r in ['Server'] + roles), 300)
        alive()
        starts = {}
        for role in roles:
            p = player('Server', role)
            start = min(route['stages'][0], key=lambda n: math.dist(p['position'][:2], route['nodes'][n]['source'][:2]))
            assert math.dist(p['position'][:2], route['nodes'][start]['source'][:2]) < 175, (
                'Server did not use the audited fixed spawn group', role, p['position'])
            starts[role] = start
        check('Both players retain their original equipped weapons in raid ' + str(number),
              lambda: all(player('Server', r).get('weapon_item') == proof['brought_weapons'][r]
                          and ownership_is(state('Server'), proof['brought_weapons'][r], 'player:EX_' + r + ':inventory', 1, 0)
                          for r in roles))
        for role in roles:
            weapon_id = proof['brought_weapons'][role]
            event = dict(raid=number, role=role, weapon=weapon_id, result='pending')
            proof.setdefault('weapon_preparation', []).append(event)
            observed = None

            def ready_ammunition(require_loaded=False):
                nonlocal observed
                snapshot = state('Server')
                authority = next(p for p in snapshot['players'] if p['name'] == 'EX_' + role)
                owner = player(role, role)
                if not ownership_is(snapshot, weapon_id, 'player:EX_' + role + ':inventory', 1, 0):
                    return False
                if not (authority.get('weapon_item') == owner.get('weapon_item') == weapon_id
                        and authority.get('combat_state') == owner.get('combat_state') == 0
                        and authority['inventory']['revision'] == owner.get('inventory', {}).get('revision')
                        and all(authority.get(k) == owner.get(k) for k in ('ammo', 'carried_ammo'))):
                    return False
                observed = preparation_ammunition(authority, weapon_id)
                return not require_loaded or observed['ammo'] > 0

            wait(ready_ammunition, role + ' original weapon ammunition replicated before preparation')
            event['before'] = observed
            if observed['ammo'] == 0:
                assert observed['carried_ammo'] > 0, (role, 'Empty original weapon has no reserve ammunition')
                command(role, 'pulse', action='Reload')
                wait(lambda: ready_ammunition(require_loaded=True), role + ' normal empty-magazine preparation reload', 12)
                validate_preparation_reload(event['before'], observed)
                event['action'] = 'ordinary Reload input'
            else:
                event['action'] = 'already loaded; no reload needed'
            event.update(after=observed, result='passed')
        return starts

    def dorm_guard(snapshot):
        matches = [guard for guard in snapshot['guards'] if guard.get('route') == route['guard_route']]
        assert len(matches) == 1, 'Expected one existing dorm-route guard; do not spawn a fixture'
        return matches[0]

    def recover_health(event):
        event['medical'] = []
        for role in roles:
            p = player('Server', role)
            kits = [item for item in p['inventory']['items'] if item['definition'] == 'Medkit']
            if p['health'] >= 100 or not kits:
                event['medical'].append(dict(role=role, result='not needed' if p['health'] >= 100 else 'no owned Medkit'))
                continue
            kit, before_health = kits[0], p['health']

            def ready_to_treat():
                alive()
                owner = player('Server', role)
                return (owner.get('combat_state') == 0 and not owner.get('falling')
                        and player(role, role)['inventory']['revision'] == owner['inventory']['revision'])

            wait(ready_to_treat, role + ' normal medical becomes available', 15)
            command(role, 'medical')

            def treated():
                alive()
                owner = player('Server', role)
                return (owner['health'] > before_health and owner.get('medical_end') == 0
                        and not any(i['id'] == kit['id'] for i in owner['inventory']['items']))

            wait(treated, role + ' owned Medkit completes normally', 15)
            event['medical'].append(dict(role=role, item=kit['id'], health_before=before_health,
                                         health_after=player('Server', role)['health'], result='passed'))

    def preserve_aim_observation(before, snapshot, shooter, guard_name, event):
        reason = 'accepted shots without observed guard damage'
        owner_snapshot = state(shooter)
        player_fields = ('name', 'pawn_name', 'position', 'capsule_half', 'health', 'shield', 'life',
                         'accepted', 'rejected', 'guard_kills', 'ammo', 'carried_ammo', 'combat_state',
                         'weapon_item', 'falling', 'traversal', 'disabled', 'inventory_open', 'aim_alpha')
        guard_fields = ('name', 'actor', 'route', 'position', 'view_position', 'health', 'dead', 'state',
                        'target', 'shots', 'ammo', 'shot_times', 'line_of_sight_to_A', 'smoke_blocked_to_A')

        def selected(source):
            return dict(world_time=source.get('world_time'), role=source.get('role'),
                        command=source.get('command'), error=source.get('error'),
                        walk_index=source.get('walk_index'), walk_count=source.get('walk_count'),
                        players=[{key: p.get(key) for key in player_fields} for p in source.get('players', [])],
                        guards=[{key: guard.get(key) for key in guard_fields}
                                for guard in source.get('guards', []) if guard.get('name') == guard_name],
                        aim_diagnostics=source.get('aim_diagnostics'))

        name = 'OvernightAimObservation-' + str(len(proof['encounters'])).zfill(2) + '.json'
        diagnostics_file = out / (shooter + '-aim-diagnostics.jsonl')
        details = dict(result='observed_no_damage_window', reason=reason, flight_diagnostics=True, shooter=shooter,
                       guard=guard_name, trigger=event['trigger'], pause=event['pause'],
                       before_server=selected(before), server=selected(snapshot), owner=selected(owner_snapshot),
                       probe_scope='Read-only local traces; not authoritative bullet impacts',
                       diagnostic_log=diagnostics_file.name, recent_aim_probes=[], screenshot=dict(result='pending'))
        if diagnostics_file.exists():
            # The driver appends concurrently; tolerate only an incomplete last
            # JSON line, while retaining the already complete probe records.
            for line in diagnostics_file.read_text(encoding='utf-8-sig').splitlines()[-8:]:
                try:
                    details['recent_aim_probes'].append(json.loads(line))
                except json.JSONDecodeError:
                    details['incomplete_probe_line'] = True
        else:
            details['diagnostic_log_missing'] = True
        event['aim_observation'] = dict(reason=reason, diagnostic_file=name)
        # Six misses can be normal spread; the full damage/death/kill-credit checks still decide success.

        def persist():
            (out / name).write_text(json.dumps(details, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            save()

        # Persist the observation before screenshot I/O; keep the ordinary fight running.
        sample(reason, snapshot, shooter=shooter, guard=guard_name, diagnostic_file=name, pause=event['pause'])
        persist()
        capture_name = 'overnight-aim-observation-' + str(len(proof['encounters'])).zfill(2)
        try:
            capture(capture_name)
            details['screenshot'] = dict(result='captured', role='A', file='A-' + capture_name + '.png')
        except Exception as exc:
            details['screenshot'] = dict(result='unavailable', role='A', error=str(exc))
        persist()

    def defend(trigger, context):
        before = state('Server')
        alive(before)
        target = dorm_guard(before)
        if target['dead']:
            assert proof.get('combat', {}).get('guard') == target['name'], 'Dead guard has no recorded player combat evidence'
            return
        assert trigger['guard'] == target['name'] and trigger['route'] == target['route']
        # Prefer the actual currently targeted player. A recorded natural
        # trigger remains evidence if perception changes during the short stop.
        current_trigger = encounter_trigger(before, route['guard_route'], roles)
        shooter = current_trigger['shooter'] if current_trigger else trigger['shooter']
        event = dict(trigger=trigger, pause=context, shooter=shooter, guard=target['name'], result='running')
        proof['encounters'].append(event)
        for role in roles:
            command(role, 'walk', points=[])
            command(role, 'inventory', open=False)
            command(role, 'aim', target='', head=False)
        stopped = state('Server')
        stopped_players = {p['name']: p for p in stopped['players']}
        event['stopped_positions'] = {r: stopped_players['EX_' + r]['position'] for r in roles}
        sample('encounter stopped on measured path', stopped, pause=context, trigger=trigger)
        command(shooter, 'aim', target=target['name'], head=False)
        command(shooter, 'input_key', key='RightMouseButton', pressed=True)

        def aimed():
            alive()
            return all(player(peer, shooter).get('aim_alpha', 0) > .99 for peer in ('Server', shooter))

        wait(aimed, 'ordinary right-mouse ADS completes on owner and authority', 5)
        event['aim_mode'] = dict(action='held RightMouseButton through ordinary input', target='body',
                                 owner_alpha=player(shooter, shooter)['aim_alpha'],
                                 server_alpha=player('Server', shooter)['aim_alpha'])
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            snapshot = state('Server')
            alive(snapshot)
            guard = dorm_guard(snapshot)
            if guard['dead']:
                break
            owner = next(p for p in snapshot['players'] if p['name'] == 'EX_' + shooter)
            if owner.get('ammo', 0) == 0:
                assert owner.get('carried_ammo', 0) > 0, 'Natural encounter exhausted the owned reserve ammunition'
                command(shooter, 'pulse', action='Reload')

                def reloaded():
                    alive()
                    return player('Server', shooter).get('ammo', 0) > 0

                wait(reloaded, 'ordinary reload during natural encounter', 12)
            command(shooter, 'pulse', action='Fire')
            time.sleep(.08)
            snapshot = state('Server')
            guard = dorm_guard(snapshot)
            owner = next(p for p in snapshot['players'] if p['name'] == 'EX_' + shooter)
            sample('natural encounter fire', snapshot, shooter=shooter, accepted=owner.get('accepted'),
                   guard={key: guard.get(key) for key in ('name', 'health', 'dead', 'state', 'target', 'shots')})
            if not event.get('aim_observation') and accepted_without_damage(flight_diagnostics, before, snapshot, shooter, target['name']):
                preserve_aim_observation(before, snapshot, shooter, target['name'], event)
        after = state('Server')
        alive(after)
        evidence = validate_encounter(before, after, shooter, target['name'])
        event.update(evidence, result='passed', world_time_after=after['world_time'])
        proof['combat'] = evidence | dict(encounter_index=len(proof['encounters'])-1)
        for role in roles:
            command(role, 'input_key', key='RightMouseButton', pressed=False)
            command(role, 'aim', target='', head=False)
        recover_health(event)
        resumed = state('Server')
        for role in roles:
            p = next(p for p in resumed['players'] if p['name'] == 'EX_' + role)
            assert not p.get('falling') and math.dist(p['position'][:2], event['stopped_positions'][role][:2]) < 65, (
                'Combat moved a player away from the measured resume position', role)
        sample('encounter complete; resume original cursor', resumed, pause=context, combat=evidence)
        save()

    def walk(starts, destination, label):
        alive()
        pair = pair_legs[(starts['A'], starts['B'], destination)]
        assert pair['order'] == ['A', 'B']
        paths = {r: pair[r]['points'] for r in roles}
        beginning = time.monotonic()
        progress = {r: 0 for r in roles}
        segment_counts = {r: 0 for r in roles}
        for role in roles:
            command(role, 'inventory', open=False)
            command(role, 'walk', points=[])
        # Only one player moves at a time. Each path was planned around the
        # other player's real logical parking node with a 180 cm reservation.
        for moving in pair['order']:
            fixed_role = 'B' if moving == 'A' else 'A'
            fixed_node = starts['B'] if moving == 'A' else destination
            fixed_position = route['nodes'][fixed_node]['position']
            assert math.dist(player('Server', fixed_role)['position'][:2], fixed_position[:2]) < 65, (
                'Stationary teammate is outside the collision reservation', label, fixed_role)
            while progress[moving] < len(paths[moving]):
                chunk = path_chunk(paths[moving], progress[moving])
                pending_trigger = None
                jump = LowLedgeJump(chunk, bridge_history[moving])
                original_completion = None
                command(moving, 'walk', points=chunk['points'])

                def observe_motion(stage, **details):
                    nonlocal pending_trigger
                    snapshot = state('Server')
                    alive(snapshot)
                    players = {p['name']: p for p in snapshot['players']}
                    moving_state = state(moving)
                    assert not moving_state.get('error'), (label, moving, moving_state.get('error'))
                    assert math.dist(players['EX_' + fixed_role]['position'][:2], fixed_position[:2]) < 65, (
                        'Stationary teammate moved out of its reserved location', label, fixed_role)
                    if pending_trigger is None:
                        pending_trigger = encounter_trigger(snapshot, route['guard_route'], roles)
                    sample(stage, snapshot, original_start=chunk['start'], original_end=chunk['end'],
                           global_completed=progress[moving], trigger=pending_trigger, **details)
                    return snapshot, moving_state, players['EX_' + moving]

                def finished():
                    nonlocal original_completion
                    snapshot, movement, participant = observe_motion(label + ':' + moving)
                    now = snapshot['world_time']
                    if jump.event is not None:
                        jump.observe_air(now, participant)
                    if jump.should_jump(now, movement, participant):
                        jump.event.update(role=moving, label=label, destination=destination)
                        proof['jumps'].append(jump.event)
                        sample('measured bridge Jump requested', snapshot, jump=jump.event)
                        save()
                        command(moving, 'pulse', action='Jump', duration=.08)
                    count = len(chunk['points'])
                    completed = movement.get('walk_count') == count and movement.get('walk_index') == count
                    if completed and original_completion is None:
                        # Cache actual driver evidence before a possible grounded
                        # return command resets its local cursor. Never synthesize it.
                        original_completion = dict(movement)
                        for event in jump.events:
                            event['original_completion'] = dict(world_time=now, walk_count=count,
                                walk_index=movement['walk_index'], falling=participant.get('falling'))
                    return completed and (not jump.events or all(event['landing'] is not None for event in jump.events)
                                          and participant.get('falling') is False)

                wait(finished, 'measured short walk ' + label + ':' + moving, 50)
                if jump.event is not None:
                    landing_snapshot = state('Server')
                    landing_player = next(p for p in landing_snapshot['players'] if p['name'] == 'EX_' + moving)
                    return_points = ledge_return_points(paths[moving], chunk, landing_player)
                    for event in jump.events:
                        event['return_points'] = return_points
                    sample('bridge landing; original cursor retained', landing_snapshot, jumps=jump.events)
                    returned = 0
                    while returned < len(return_points):
                        return_chunk = path_chunk(return_points, returned)
                        command(moving, 'walk', points=return_chunk['points'])

                        def return_finished():
                            snapshot, movement, participant = observe_motion('bridge grounded return',
                                return_start=return_chunk['start'], return_end=return_chunk['end'])
                            jump.check_budget(snapshot['world_time'])
                            return (movement.get('walk_count') == len(return_chunk['points'])
                                    and movement.get('walk_index') == len(return_chunk['points'])
                                    and at_ground_point(participant, return_chunk['points'][-1]))

                        wait(return_finished, 'return along measured nodes to original segment endpoint', 30)
                        returned = complete_path_chunk(return_chunk, returned, state(moving), player('Server', moving))
                        for event in jump.events:
                            event['return_segments'].append(dict(points=return_chunk['points'], completed=returned,
                                world_time=state('Server')['world_time']))
                wait(lambda: at_ground_point(player('Server', moving), chunk['points'][-1]),
                     'grounded short-segment endpoint ' + label + ':' + moving, 8)
                progress[moving] = complete_path_chunk(chunk, progress[moving], original_completion, player('Server', moving))
                if jump.event is not None:
                    now = state('Server')['world_time']
                    jump.check_budget(now)
                    for event in jump.events:
                        event.update(result='passed', original_completed=progress[moving],
                                     endpoint=chunk['points'][-1], world_time_after=now,
                                     role_recovery_seconds=now - bridge_history[moving][0]['trigger']['world_time'])
                    sample('normal Jump recovery completed at original grounded endpoint', jumps=jump.events)
                    save()
                segment_counts[moving] += 1
                context = dict(label=label, destination=destination, moving=moving,
                               completed=dict(progress), original_start=chunk['start'], original_end=chunk['end'],
                               endpoint=chunk['points'][-1])
                sample('measured segment complete', original_segment=context)
                pending_trigger = pending_trigger or encounter_trigger(state('Server'), route['guard_route'], roles)
                if pending_trigger:
                    defend(pending_trigger, context)
            check('Authoritative ground and endpoint ' + label + ':' + moving, lambda: (
                not player('Server', moving).get('falling')
                and math.dist(player('Server', moving)['position'][:2], paths[moving][-1][:2]) < 65
                and abs(player('Server', moving)['position'][2] - player('Server', moving)['capsule_half'] - paths[moving][-1][2]) < 90), 8)
        proof['legs'].append(dict(destination=destination, label=label,
                                 seconds=round(time.monotonic() - beginning, 2), result='passed',
                                 points={r: len(paths[r]) for r in roles}, completed=progress, segments=segment_counts))
        save()
        return {'A': destination, 'B': pair['b_end']}

    def loot(group, current):
        matches = [box for box in state('Server')['loot'] if box.get('source_point') in route['nodes']
                   and route['nodes'][box['source_point']].get('group') == group]
        assert len(matches) == 1, (group, 'expected one selected candidate', len(matches))
        box = matches[0]
        current = walk(current, box['source_point'], group)
        wait(lambda: any(b['id'] == box['id'] for b in state('A')['loot']), 'selected container replicated')
        search_context = dict(label=group, stage='search', destination=box['source_point'], parked_nodes=current)

        def open_and_search():
            while True:
                command('A', 'open_loot', target=box['id'])
                wait(lambda: player('A', 'A').get('loot_id') == box['id'],
                     'Normal server range/visibility permits search ' + group)
                pending = None

                def revealed_or_targeted():
                    nonlocal pending
                    snapshot = state('Server')
                    alive(snapshot)
                    pending = encounter_trigger(snapshot, route['guard_route'], roles)
                    view = player('A', 'A').get('loot_view', {}).get('items', [])
                    return pending is not None or view and all(i['definition'] not in ('None', '') for i in view)

                wait(revealed_or_targeted, 'normal progressive search ' + group, 30)
                if pending is None:
                    return
                defend(pending, search_context)
                # The production search remembers already revealed identities.
                # Reopen the same real box after combat; no new contents are made.

        open_and_search()
        definitions = {'7F_WestDorm_A': ['Bandage'], '7F_WestDorm_B': ['Splint'],
                       '7F_LivingArea_B': ['Document', 'Tools']}[group]
        for definition in definitions:
            pending = encounter_trigger(state('Server'), route['guard_route'], roles)
            if pending:
                defend(pending, search_context)
                open_and_search()
            # Consecutive pickup RPCs must use the replicated revisions from the
            # preceding normal transaction, not a stale open-container snapshot.
            wait(lambda: player('A', 'A')['inventory']['revision'] == player('Server', 'A')['inventory']['revision']
                 and player('A', 'A')['loot_view']['revision'] == next(
                     b['contents']['revision'] for b in state('Server')['loot'] if b['id'] == box['id']),
                 'pickup revisions replicated ' + definition)
            item = next(i for i in player('A', 'A')['loot_view']['items'] if i['definition'] == definition)
            before = state('A').get('item_sequence', 0)
            command('A', 'item', item=item['id'], from_loot=True, quantity=item['quantity'], auto=True)
            wait(lambda: state('A').get('item_sequence', 0) > before, 'pickup result ' + definition)
            assert not state('A').get('item_message'), state('A').get('item_message')
            check('One authoritative owner after taking ' + definition,
                  lambda: ownership_is(state('Server'), item['id'], 'player:EX_A:inventory', item['quantity']))
            proof['acquired'].append(dict(item=item['id'], definition=definition, quantity=item['quantity'],
                                          source_point=box['source_point'], sell_after_return=definition == 'Tools'))
        capture('overnight-' + group)
        command('A', 'inventory', open=False)
        return current

    try:
        current = start_raid(1)
        wait(lambda: len([guard for guard in state('Server')['guards'] if guard.get('route') == route['guard_route']]) == 1,
             'the authored dorm guard spawned normally', 30)
        initial_guard = dorm_guard(state('Server'))
        assert not initial_guard['dead'], 'Authored guard was already defeated before this route began'
        proof['authored_guard'] = dict(name=initial_guard['name'], route=initial_guard['route'])
        initial_raid = authoritative_raid(player('Server', 'A'))
        assert authoritative_raid(player('Server', 'B')) == initial_raid, 'Players belong to different raids'
        capture('overnight-spawn')
        command('A', 'input_key', key='M', pressed=True)
        command('A', 'input_key', key='M', pressed=False)
        check('Tactical map opens through input', lambda: 'ExtractionTacticalMap' in state('A').get('visible_widgets', []))
        capture('overnight-map')
        command('A', 'input_key', key='Escape', pressed=True)
        command('A', 'input_key', key='Escape', pressed=False)
        for group in route['search_groups']:
            current = loot(group, current)
        current = walk(current, route['guard_route'], 'existing dorm guard encounter')
        snapshot = state('Server')
        if not proof.get('combat'):
            guard = dorm_guard(snapshot)
            if not guard['dead']:
                trigger = encounter_trigger(snapshot, route['guard_route'], roles) or dict(
                    shooter='A', guard=guard['name'], route=guard['route'], world_time=snapshot['world_time'],
                    reason='authored guard encounter node reached')
                defend(trigger, dict(stage='authored guard node', parked_nodes=current))
        # DefeatCombatant reclaims guard actors after 10 seconds. The completed
        # encounter, not a corpse that must remain alive in memory, proves combat.
        proof['combat']['actor_at_authored_node'] = confirm_completed_guard(state('Server'), route['guard_route'], proof)
        proof['combat']['authored_node_visited'] = True
        capture('overnight-combat')
        current = walk(current, route['exit'], 'dorm to square through measured floors')
        exit_snapshot = state('Server')
        exit_state = next(e for e in exit_snapshot.get('exits', []) if e['id'] == route['exit'])
        assert (exit_state['opens_after'], exit_state['closes_after'], exit_state['hold_seconds']) == (900, 1020, 10), (
            'Square production opening, closing or hold timing changed')
        remaining = exit_state['round_started'] + exit_state['opens_after'] - exit_snapshot['world_time']
        assert remaining > exit_state['hold_seconds'], 'Arrived too late to verify the closed-to-open transition'
        capture('overnight-square-closed')
        last_open_sample = 0.0
        opening = exit_state['round_started'] + exit_state['opens_after']

        def square_open():
            nonlocal last_open_sample
            opened = square_is_open(lambda: state('Server'), roles, route['exit'], opening)
            if time.monotonic() - last_open_sample >= 2:
                sample('waiting for normal square opening')
                last_open_sample = time.monotonic()
            return opened

        wait(square_open, 'normal 900-second square opening', max(1, remaining) + 20)
        hold_snapshot = None

        def both_holds_started():
            nonlocal hold_snapshot
            snapshot = state('Server')
            players = {p['name']: p for p in snapshot['players']}
            if not all(players['EX_' + r].get('extract_at', 0) > 0 for r in roles):
                return False
            hold_snapshot = snapshot
            return True

        wait(both_holds_started, 'Both ordinary hold timers begin', 3)
        hold_players = {p['name']: p for p in hold_snapshot['players']}
        holds = {role: hold_deadline(hold_players['EX_' + role], route['exit'], opening, exit_state['hold_seconds'])
                 for role in roles}
        proof['hold_deadlines'] = holds
        proof['hold_observation'] = dict(nominal_poll_seconds=.15,
            boundary='Sampled authority snapshots, not per-frame observation; file publication and scheduling may add latency.')
        # Do not issue synchronous screenshots during the 10-second hold:
        # send() can wait up to 30 seconds and create an observation blind spot.

        def settled_after_hold():
            authority_settled = settled_after_deadlines(lambda: state('Server'), roles, route['exit'], holds)
            sample('ordinary extraction hold')
            return authority_settled and all(state(r).get('account', {}).get('outcome') == 1 for r in roles)

        check('Both players extract after the full production hold timer', settled_after_hold, exit_state['hold_seconds'] + 20)
        capture('overnight-settlement')
        check('Ordinary round returns both accounts to lobby', lambda: all(
            state(r).get('map') == '/Game/Maps/Lobby' and not state(r)['account'].get('locked') for r in roles), 60)
        for acquired in proof['acquired']:
            check('Exported identity and quantity have one stash owner ' + acquired['definition'],
                  lambda a=acquired: ownership_is(state('Server'), a['item'], 'player:EX_A:stash', a['quantity'])
                  and sum(i['id'] == a['item'] for i in owned_items(state('A')['account'])) == 1)
        for role in roles:
            check(role + ' original weapon returns once to the stash',
                  lambda r=role: ownership_is(state('Server'), proof['brought_weapons'][r], 'player:EX_' + r + ':stash', 1))
        proof['first_raid'] = 'completed'
        capture('overnight-return-lobby')

        # Document is deliberately unsellable in the production rules. Sell the
        # extra Tools stack from this raid's same valuable container instead.
        sold = next(a for a in proof['acquired'] if a['sell_after_return'])
        before_account = player('Server', 'A')['account']
        sale_item = next(i for i in before_account['stash']['items'] if i['id'] == sold['item'])
        quote = sale_item.get('sell_price')
        assert isinstance(quote, (int, float)) and quote > 0, 'Missing or invalid authoritative production sale quote'
        before_credits = before_account['progression']['credits']
        before_revision = before_account['revision']
        wait(lambda: state('A')['account']['revision'] == before_revision, 'Sale revision replicated')
        command('A', 'economy', kind='sell', item=sold['item'], quantity=sold['quantity'])
        check('Normal sale removes the entire identity and credits the exact production quote',
              lambda: sale_is_complete(state('Server'), 'A', sold['item'], before_credits, quote, before_revision)
              and state('A')['account']['revision'] > before_revision
              and state('A')['account']['progression']['credits'] == before_credits + quote
              and not any(i['id'] == sold['item'] for i in owned_items(state('A')['account'])))
        proof['sale'] = dict(item=sold['item'], definition=sold['definition'], quantity=sold['quantity'],
                             quote=quote, credits_before=before_credits, credits_after=before_credits + quote,
                             result='passed', ownership_scope='all live server player, account, loot and guard containers')
        capture('overnight-sale')
        start_raid(2)
        # The stash contains this raid's exported loot; ordinary preparation must
        # not duplicate it or silently move it into the newly spawned inventory.
        for acquired in proof['acquired']:
            if acquired['sell_after_return']:
                check('Next raid cannot restore a sold item',
                      lambda a=acquired: not item_owners(state('Server'), a['item'])
                      and not any(i['id'] == a['item'] for i in owned_items(state('A')['account'])))
            else:
                check('Next raid preserves exported identity and quantity once ' + acquired['definition'],
                      lambda a=acquired: ownership_is(state('Server'), a['item'], 'player:EX_A:stash', a['quantity'])
                      and sum(i['id'] == a['item'] for i in owned_items(state('A')['account'])) == 1)
        new_raid = authoritative_raid(player('Server', 'A'))
        assert new_raid != initial_raid and authoritative_raid(player('Server', 'B')) == new_raid, 'Expected a new shared raid identity'
        proof['raid_ids'] = [initial_raid, new_raid]
        proof.update(result='passed', second_raid='normal preparation and spawn verified', second_raid_completed=False)
        capture('overnight-second-raid')
        save()
    except Exception as exc:
        proof.update(result='failed', error=str(exc))
        for jump_event in proof['jumps']:
            if jump_event['result'] != 'passed':
                jump_event.update(result='failed', error=str(exc))
        # Preserve the final recovery observation before clearing real input.
        if any(event['result'] == 'failed' for event in proof['jumps']):
            save()
        for role in roles:
            try:
                send(role, 'input_key', key='RightMouseButton', pressed=False)
                send(role, 'walk', points=[])
            except Exception:
                pass
        save()
        raise
    finally:
        motion.close()
