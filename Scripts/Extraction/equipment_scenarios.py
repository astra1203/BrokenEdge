"""Phase 6A acceptance: real supply crates, item RPCs, input and bullet traces.

Fixtures position participants and restore health for a bounded damage comparison.
They never assign armor damage, trauma, attachment effects or medical outcomes.
"""
import math
import time


def run(g):
    send, check, wait, state, player = (g[k] for k in ('send', 'check', 'wait', 'state', 'player'))
    roles = g['ROLES']
    peers = ['Server'] + roles
    positions = {r: [5700 + 160 * i, 5000, 180] for i, r in enumerate(roles)}
    positions.update(A=[5700, 4500, 180], B=[5700, 5100, 180], C=[5860, 4500, 180])

    def place(role, point=None, health=100, shield=0):
        send('Server', 'fixture', players=[{'name': 'EX_' + role, 'position': point or positions[role],
                                          'health': health, 'shield': shield}])
        time.sleep(.4)

    def items(role, peer='Server'):
        return player(peer, role)['inventory']['items']

    def item(role, definition):
        return next(i for i in items(role) if i['definition'] == definition)

    def gun(role):
        return next(i for i in items(role) if i['slot'] == 0)

    def operation(role, ident, **fields):
        wait(lambda: player(role, role)['inventory'] == player('Server', role)['inventory'], 'owner inventory sync')
        before = state(role).get('item_sequence', 0)
        send(role, 'item', item=ident, **fields)
        wait(lambda: state(role).get('item_sequence', 0) > before, 'equipment operation response')
        return state(role).get('item_message', '')

    def acquire(role, crate, definitions):
        point = crate['position']
        place(role, [point[0] + 90, point[1], point[2] + 90])
        send(role, 'aim', target='', head=False)
        send(role, 'open_loot', target=crate['id'])
        wait(lambda: player(role, role).get('loot_id') == crate['id'], 'equipment supply opened')
        wait(lambda: all(i['definition'] != 'None' for i in player(role, role)['loot_view']['items']), 'supply searched', 20)
        for definition in definitions:
            found = next(i for i in player(role, role)['loot_view']['items'] if i['definition'] == definition)
            assert not operation(role, found['id'], from_loot=True, quantity=found['quantity'])
            check(role + ' acquired ' + definition + ' with original identity', lambda f=found: any(i['id'] == f['id'] for i in items(role)))
        send(role, 'inventory', open=False)
        place(role)

    def fire_at(shooter, target, bone, expected=None):
        send(shooter, 'inventory', open=False)
        send(shooter, 'aim', target='EX_' + target, head=False, bone=bone)
        time.sleep(.5)
        for attempt in range(12):
            old, victim = player('Server', shooter).copy(), player('Server', target).copy()
            send(shooter, 'pulse', action='Fire')
            check('One server shot spends one round', lambda: player('Server', shooter)['accepted'] == old['accepted'] + 1)
            check('Equipment shot ammo agrees on all peers', lambda: all(player(p, shooter).get('ammo') == old['ammo'] - 1 for p in peers))
            time.sleep(.3)
            current = player('Server', target)
            if current['bullet_sequence'] > victim['bullet_sequence'] and (expected is None or math.isclose(current['hit_loss'], expected, abs_tol=.02)):
                check('Real bullet ' + bone + ' health damage ' + str(expected), lambda: True)
                return current
            g['report'].setdefault('equipment_scatter_attempts', []).append({'before': victim, 'after': current, 'bone': bone, 'attempt': attempt})
        raise AssertionError('No requested hit classification: ' + bone)

    def treat(role, definition):
        ident = item(role, definition)['id']
        assert not operation(role, ident, kind='use')
        check(definition + ' begins without consuming item', lambda: player('Server', role)['medical_end'] > 0 and any(i['id'] == ident for i in items(role)))
        return ident

    if len(roles) == 6:
        check('Six humans and four AI coexist with equipment', lambda: len(state('Server')['players']) == 6 and all(len(state(p)['guards']) == 4 for p in peers))
        check('Equipment map retains complete patrol navigation', lambda: len(state('Server')['nav_paths']) == 6 and all(p['valid'] for p in state('Server')['nav_paths']))
        check('Equipment map guards reach patrol points', lambda: all(a['arrivals'] >= 1 for a in state('Server')['guards']), 80)
    crates = {tier: next(c for c in state('Server')['loot'] if any(i['definition'] == 'Helmet' + str(tier) for i in c['contents']['items'])) for tier in (2, 3)}
    check('Two map supplies contain exactly the approved eight items', lambda: all(len(c['contents']['items']) == 8 for c in crates.values()))
    acquire('A', crates[2], ['Helmet2', 'Vest2', 'Bandage', 'Splint'])
    for definition, slot in [('Helmet2', 2), ('Vest2', 3)]:
        assert not operation('A', item('A', definition)['id'], slot=slot)
    acquire('B', crates[2], ['RedDot', 'VerticalGrip', 'Compensator', 'AmmoAP'])
    acquire('A', crates[3], ['Bandage', 'Splint'])
    acquire('B', crates[3], ['Helmet3', 'Vest3'])
    for definition, slot in [('Helmet3', 2), ('Vest3', 3)]:
        assert not operation('B', item('B', definition)['id'], slot=slot)
    for r in roles:
        place(r)
    parts = {d: item('B', d)['id'] for d in ['RedDot', 'VerticalGrip', 'Compensator']}
    for definition, ident in parts.items():
        assert not operation('B', ident, kind='attach', weapon=gun('B')['id'])
        check(definition + ' appears on all weapon actors', lambda d=definition: all(d in player(p, 'B').get('weapon_parts', []) for p in peers))
    check('Server combines both scatter modifiers multiplicatively', lambda: math.isclose(gun('B')['scatter_scale'], .68, abs_tol=.00001))
    assert not operation('B', gun('B')['id'], kind='detach', attachment_slot='grip')
    check('Detaching preserves identity and removes only grip effect', lambda: item('B', 'VerticalGrip')['id'] == parts['VerticalGrip'] and math.isclose(gun('B')['scatter_scale'], .85, abs_tol=.00001))
    assert operation('B', parts['VerticalGrip'], kind='attach', weapon=gun('A')['id'])
    assert not operation('B', parts['VerticalGrip'], kind='attach', weapon=gun('B')['id'])
    send('B', 'inventory', open=True)
    send('B', 'screenshot', name='equipment-inventory')
    send('B', 'inventory', open=False)
    send('B', 'aim', target='EX_A', head=False)
    send('B', 'screenshot', name='equipment-hip')
    send('B', 'pulse', action='Aim', duration=2)
    time.sleep(.8)
    send('B', 'screenshot', name='equipment-ads')
    time.sleep(1.5)
    send('B', 'pulse', action='ToggleCameraView')
    time.sleep(.4)
    send('B', 'screenshot', name='equipment-third-person')
    send('B', 'pulse', action='ToggleCameraView')
    place('A', shield=80)
    vest_before = item('A', 'Vest2')['durability']
    fire_at('B', 'A', 'Spine2', 8)
    check('Torso armor spends absorbed damage; Extraction ignores generic shield', lambda: item('A', 'Vest2')['durability'] < vest_before and player('Server', 'A')['shield'] == 80)
    check('Subthreshold damage creates no trauma', lambda: not player('Server', 'A')['bleeding'] and not player('Server', 'A')['fracture'])
    fire_at('B', 'A', 'Head', 16)
    check('Protected head hit can still cause bleeding at threshold', lambda: all(player(p, 'A')['bleeding'] for p in peers))
    bandage = treat('A', 'Bandage')
    check('Bleeding ticks do not cancel three second bandage', lambda: not player('Server', 'A')['bleeding'] and all(i['id'] != bandage for i in items('A')), 8)
    body_ammo = gun('B')['magazine']
    reserve_before = sum(i['quantity'] for i in items('B') if i['definition'] == 'Ammo')
    assert operation('B', gun('B')['id'], kind='select_ammo', ammo_type='AmmoAP')
    assert not operation('B', gun('B')['id'], kind='unload')
    check('Unload returns actual ordinary round count', lambda: gun('B')['magazine'] == 0 and sum(i['quantity'] for i in items('B') if i['definition'] == 'Ammo') == reserve_before + body_ammo)
    assert not operation('B', gun('B')['id'], kind='select_ammo', ammo_type='AmmoAP')
    send('B', 'pulse', action='Reload')
    check('AP reload commits the selected homogeneous magazine', lambda: gun('B')['magazine'] == 30 and gun('B')['ammo_type'] == 'AmmoAP' and all(player(p, 'B')['ammo_type'] == 'AmmoAP' for p in peers), 12)
    wait(lambda: player('Server', 'B')['combat_state'] == 0, 'reload finishes')
    place('A')
    fire_at('B', 'A', 'Spine2', 13.6)
    check('AP torso hit remains below trauma threshold', lambda: not player('Server', 'A')['bleeding'])
    normal_speed = player('Server', 'A')['walk_speed']
    normal_crouch = player('Server', 'A')['crouch_speed']
    fire_at('B', 'A', 'LeftLeg', 17)
    check('Leg hit bypasses torso armor and creates both statuses', lambda: all(player(p, 'A')['bleeding'] and player(p, 'A')['fracture'] for p in peers))
    check('Fracture reduces walk and crouch speed once', lambda: math.isclose(player('Server', 'A')['walk_speed'], normal_speed * .7, abs_tol=.02) and math.isclose(player('Server', 'A')['crouch_speed'], normal_crouch * .7, abs_tol=.02))
    send('A', 'pulse', action='Sprint', duration=.6)
    check('Fracture prevents sprint speed', lambda: player('Server', 'A')['walk_speed'] <= normal_speed * .7 + .02)
    before = player('Server', 'A')['health']
    med = treat('A', 'Medkit')
    check('Medkit heals but preserves bleeding and fracture', lambda: all(i['id'] != med for i in items('A')) and player('Server', 'A')['health'] > before and player('Server', 'A')['bleeding'] and player('Server', 'A')['fracture'], 9)
    splint = treat('A', 'Splint')
    send('A', 'pulse', action='Move', duration=.6)
    check('Movement cancels splint without consuming it', lambda: player('Server', 'A')['medical_end'] == 0 and any(i['id'] == splint for i in items('A')) and player('Server', 'A')['fracture'])
    place('A')
    bandage = treat('A', 'Bandage')
    fire_at('B', 'A', 'Spine2')
    check('External bullet cancels bandage without consumption', lambda: player('Server', 'A')['medical_end'] == 0 and any(i['id'] == bandage for i in items('A')))
    if len(roles) >= 3:
        place('C')
        send('C', 'ability', kind='heal', target='EX_A')
        check('Heal ability also leaves trauma intact', lambda: player('Server', 'C')['heal_ready'] > 0 and player('Server', 'A')['bleeding'] and player('Server', 'A')['fracture'])
        place('A', health=10)
        send('B', 'aim', target='EX_A', head=False, bone='LeftLeg')
        for _ in range(8):
            send('B', 'pulse', action='Fire')
            time.sleep(.4)
            if player('Server', 'A')['life'] == 5:
                break
        check('Wounded player downs while ally lives', lambda: player('Server', 'A')['life'] == 5)
        down = player('Server', 'A')['down_health']
        time.sleep(3)
        check('Normal bleeding pauses while downed', lambda: player('Server', 'A')['down_health'] == down and player('Server', 'A')['life'] == 5)
        send('C', 'ability', kind='revive', target='EX_A')
        check('Revive clears bleeding and retains leg fracture', lambda: player('Server', 'A')['life'] == 0 and not player('Server', 'A')['bleeding'] and player('Server', 'A')['fracture'], 10)
    else:
        bandage = treat('A', 'Bandage')
        check('Second bandage resolves persistent bleeding', lambda: not player('Server', 'A')['bleeding'] and all(i['id'] != bandage for i in items('A')), 8)
    splint = treat('A', 'Splint')
    check('Five second splint consumes once and restores both speeds', lambda: not player('Server', 'A')['fracture'] and all(i['id'] != splint for i in items('A')) and math.isclose(player('Server', 'A')['walk_speed'], normal_speed, abs_tol=.02) and math.isclose(player('Server', 'A')['crouch_speed'], normal_crouch, abs_tol=.02), 10)
    exported = gun('B').copy()
    center = state('Server')['exit_position']
    send('B', 'aim', target='', head=False)
    place('B', [center[0], center[1], center[2] + 90])
    check('Assembled AP rifle exports with every independent part', lambda: state('B')['account']['outcome'] == 1 and any(i['id'] == exported['id'] and i['attachments'] == exported['attachments'] and i['ammo_type'] == 'AmmoAP' and i['magazine'] == exported['magazine'] for i in state('B')['account']['stash']['items']), 22)
    send('Server', 'end')
    check('Equipment raid returns to unready lobby', lambda: all(state(p).get('map') == '/Game/Maps/Lobby' for p in peers) and all(not player('Server', r)['ready'] for r in roles), 40)
    account = state('B')['account']
    send('B', 'storage', item=exported['id'], kind='detach', attachment_slot='optic', **{'from': 0, 'to': 0})
    check('Unready stash can detach exported original optic', lambda: state('B')['account']['revision'] > account['revision'] and any(i['id'] == parts['RedDot'] for i in state('B')['account']['stash']['items']))
    send('B', 'stash_ui')
    send('B', 'screenshot', name='equipment-stash')
    send('B', 'stash_ui')
    g['begin_round']()
    check('Next raid starts with no residual trauma on fresh pawns', lambda: all(not player('Server', r)['bleeding'] and not player('Server', r)['fracture'] for r in roles))
    send('Server', 'end')
    check('Second equipment raid also returns normally', lambda: all(state(p).get('map') == '/Game/Maps/Lobby' for p in peers), 40)
