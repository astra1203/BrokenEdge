"""Phase-3 acceptance through production UI handlers/RPCs in independent processes."""
import json
import os
import time


def run_inventory_suite(api):
    send, check, wait = (api[k] for k in ('send', 'check', 'wait'))
    state, player = (api[k] for k in ('state', 'player'))

    def items(role='A', peer='Server'):
        return player(peer, role).get('inventory', {}).get('items', [])

    def total(definition, role='A', peer='Server'):
        return sum(i['quantity'] for i in items(role, peer) if i['definition'] == definition)

    def item(ident, role='A', peer='Server'):
        return next((i for i in items(role, peer) if i['id'] == ident), None)

    def sync(role='A'):
        wait(lambda: player(role, role).get('inventory') == player('Server', role).get('inventory'), role + ' owner inventory matches authority')

    def action(role, ident, **fields):
        sync(role)
        previous = state(role).get('item_sequence', 0)
        send(role, 'item', item=ident, **fields)
        wait(lambda: state(role).get('item_sequence', 0) > previous, role + ' inventory response')
        sync(role)
        return state(role).get('item_message', '')

    def close(role='A'):
        send(role, 'inventory', open=False)
        wait(lambda: not player(role, role).get('inventory_open'), role + ' closes inventory')

    def place(players):
        send('Server', 'fixture', players=[{'name': 'EX_' + role, 'position': pos, 'health': hp, 'shield': shield}
                                          for role, pos, hp, shield in players])
        time.sleep(.35)

    def loot(label):
        return next((v for v in state('Server').get('loot', []) if v['label'] == label), None)

    def known(role):
        return [i for i in player(role, role).get('loot_view', {}).get('items', []) if i['definition'] != 'None']

    def open_box(role, name):
        wait(lambda: any(v['id'] == name for v in state(role).get('loot', [])), role + ' sees container actor')
        send(role, 'open_loot', target=name)
        wait(lambda: player(role, role).get('loot_id') == name, role + ' has active container session')

    def invariant(label):
        entries = [i for p in state('Server')['players'] for i in p.get('inventory', {}).get('items', [])]
        entries += [i for box in state('Server').get('loot', []) for i in box['contents']['items']]
        ids = [i['id'] for i in entries]
        check(label, lambda: len(ids) == len(set(ids)) and all(i['quantity'] > 0 for i in entries))

    check('Both players receive 120 real reserve rounds and two medical items',
          lambda: all(total('Ammo', r) == 120 and total('Medkit', r) == 2 for r in ['A', 'B']))
    sync('A'); sync('B')
    check('Other players inventories are not replicated to either client',
          lambda: not items('B', 'A') and not items('A', 'B'))
    check('World containers do not replicate hidden contents',
          lambda: all(not box['contents']['items'] for r in ['A', 'B'] for box in state(r)['loot']))
    invariant('Initial identities have exactly one container owner')

    # Actual Slate mouse routing exercises hit-testing, drag, rotation and context menu.
    send('A', 'inventory', open=True)
    check('Inventory opens through production controller', lambda: player('A', 'A').get('inventory_open'))
    send('A', 'screenshot', name='inventory-initial')
    medical = next(i for i in items() if i['definition'] == 'Medkit')
    x, y = 140 + medical['x'] * 58 + 15, 240 + medical['y'] * 58 + 15
    send('A', 'mouse', x=x, y=y, action='down')
    send('A', 'key', key='R')
    send('A', 'mouse', x=155, y=313, action='move')
    send('A', 'screenshot', name='inventory-drag-rotated')
    send('A', 'mouse', x=155, y=313, action='up')
    check('Mouse drag and R rotation commit correct grid cells', lambda: item(medical['id'])['rotated'] and item(medical['id'])['x'] == 0 and item(medical['id'])['y'] == 1)
    sync()
    ammo = next(i for i in items() if i['definition'] == 'Ammo')
    x, y = 140 + ammo['x'] * 58 + 15, 240 + ammo['y'] * 58 + 15
    send('A', 'mouse', x=x, y=y, action='down', button='right')
    send('A', 'mouse', x=x, y=y, action='up', button='right')
    send('A', 'screenshot', name='inventory-context-menu')
    send('A', 'mouse', x=x + 20, y=y + 110, action='down')
    send('A', 'mouse', x=x + 20, y=y + 110, action='up')
    send('A', 'key', key='Up')
    send('A', 'screenshot', name='inventory-split-quantity')
    send('A', 'key', key='Enter')
    send('A', 'mouse', x=387, y=371, action='down')
    send('A', 'mouse', x=387, y=371, action='up')
    check('Context split creates 31 and 29 without changing total ammunition',
          lambda: sorted(i['quantity'] for i in items() if i['definition'] == 'Ammo') == [29, 31, 60])
    split = next(i for i in items() if i['definition'] == 'Ammo' and i['quantity'] == 31)
    assert not action('A', split['id'], quantity=31, merge=ammo['id'])
    check('Merge retires split identity and conserves ammunition', lambda: item(split['id']) is None and total('Ammo') == 120)
    close()

    # A live shot must survive storage, recreation of the weapon actor, and pickup by another player.
    send('A', 'aim', target='', head=False)
    initial_gun = player('Server', 'A')['weapon_item']
    send('A', 'pulse', action='Fire')
    check('Accepted shot updates authoritative weapon instance', lambda: item(initial_gun)['magazine'] == 29)
    assert not action('A', initial_gun, slot=-1)
    check('Storing weapon removes equipped actor', lambda: not player('Server', 'A').get('weapon') and item(initial_gun)['slot'] == -1)
    assert not action('A', initial_gun, slot=0)
    check('Re-equipping preserves identity and 29 loaded rounds', lambda: player('A', 'A').get('weapon_item') == initial_gun and player('A', 'A').get('ammo') == 29)
    assert not action('A', initial_gun, kind='drop')
    check('World drop preserves the same loaded weapon instance', lambda: any(i['id'] == initial_gun and i['magazine'] == 29 for box in state('Server')['loot'] for i in box['contents']['items']))
    dropped = next(box for box in state('Server')['loot'] if any(i['id'] == initial_gun for i in box['contents']['items']))
    pos = dropped['position']
    place([('A', [1400, 4200, 500], 80, 0), ('B', [pos[0] - 70, pos[1], pos[2] + 90], 80, 0)])
    send('B', 'open_loot', target=dropped['id'])
    check('Other player receives dropped gun in secondary slot', lambda: item(initial_gun, 'B') is not None and item(initial_gun, 'B')['slot'] == 1 and item(initial_gun, 'B')['magazine'] == 29)
    assert not action('B', initial_gun, kind='drop')
    wait(lambda: any(i['id'] == initial_gun for box in state('Server')['loot'] for i in box['contents']['items']), 'gun dropped back')
    dropped = next(box for box in state('Server')['loot'] if any(i['id'] == initial_gun for i in box['contents']['items']))
    pos = dropped['position']
    place([('B', [1400, 4100, 500], 80, 0), ('A', [pos[0] - 70, pos[1], pos[2] + 90], 80, 0)])
    send('A', 'open_loot', target=dropped['id'])
    check('Original owner reclaims same gun and magazine', lambda: item(initial_gun) is not None and item(initial_gun)['slot'] == 0 and player('Server', 'A').get('ammo') == 29)

    place([('A', [1400, 4200, 500], 80, 0), ('B', [1400, 4100, 500], 80, 0)])
    send('Server', 'spawn_loot', label='ConcurrentBox', position=[1500, 4200, 425], definition='Tools', count=3)
    box = loot('ConcurrentBox')
    wait(lambda: loot('ConcurrentBox') is not None, 'container fixture spawned')
    box = loot('ConcurrentBox')
    all_ids = [i['id'] for i in box['contents']['items']]
    open_box('A', box['id'])
    check('First search initially masks undiscovered identities and definitions', lambda: any(i['definition'] == 'None' and set(i['id'].replace('-', '')) == {'0'} for i in player('A', 'A')['loot_view']['items']))
    assert action('A', all_ids[-1], from_loot=True)
    check('Forged request cannot take an unrevealed item', lambda: len(loot('ConcurrentBox')['contents']['items']) == 3 and total('Tools') == 0)
    wait(lambda: len(known('A')) >= 1, 'first item revealed')
    revealed_ids = {i['id'] for i in known('A')}
    close()
    time.sleep(1.2)
    open_box('A', box['id'])
    check('Closing and reopening preserves completed reveals', lambda: revealed_ids.issubset({i['id'] for i in known('A')}))
    wait(lambda: len(known('A')) == 3, 'all A items revealed')
    open_box('B', box['id'])
    check('Second player starts an independent search', lambda: len(known('B')) < 3 and len(known('A')) == 3)
    wait(lambda: len(known('B')) == 3, 'all B items revealed')
    send('A', 'screenshot', name='container-revealed')

    # Publish both requests without waiting for either client's response.
    race_id = all_ids[0]
    revision = loot('ConcurrentBox')['contents']['revision']
    before_sequences = {r: state(r).get('item_sequence', 0) for r in ['A', 'B']}
    for role in ['A', 'B']:
        command_id = api['commands'].get(role, 0) + 1
        api['commands'][role] = command_id
        file = api['OUT'] / (role + '-command.json')
        temp = file.with_suffix('.race.tmp')
        temp.write_text(json.dumps({'id': command_id, 'op': 'item', 'item': race_id, 'from_loot': True,
                                    'loot_revision': revision}), encoding='utf-8')
        for attempt in range(100):
            try:
                os.replace(temp, file)
                break
            except PermissionError:
                if attempt == 99:
                    raise
                time.sleep(.01)
    wait(lambda: all(state(r).get('item_sequence', 0) > before_sequences[r] for r in ['A', 'B']), 'both competing requests answered')
    check('Simultaneous take has exactly one winner', lambda: sum(item(race_id, r) is not None for r in ['A', 'B']) == 1 and len(loot('ConcurrentBox')['contents']['items']) == 2)
    winner = next(r for r in ['A', 'B'] if item(race_id, r))
    before = total('Tools', winner)
    send(winner, 'item', duplicate=True)
    time.sleep(.6)
    check('Duplicate request cannot execute again', lambda: total('Tools', winner) == before and bool(state(winner).get('item_message')))
    invariant('Competing transfers leave every identity with one owner')
    close('A'); close('B')
    remaining = loot('ConcurrentBox')['contents']['items'][0]['id']
    assert action('A', remaining, from_loot=True)
    check('Closed search session rejects later transfer', lambda: len(loot('ConcurrentBox')['contents']['items']) == 2)
    open_box('A', box['id'])
    place([('A', [1000, 4200, 500], 80, 0)])
    check('Walking out of range closes container on server and owner', lambda: not player('Server', 'A')['inventory_open'] and not player('A', 'A')['inventory_open'])
    assert action('A', remaining, from_loot=True)
    check('Out-of-range transfer preserves contents', lambda: len(loot('ConcurrentBox')['contents']['items']) == 2)

    place([('A', [1400, 4200, 500], 80, 0)])
    send('Server', 'occluder', enabled=True)
    previous_session = player('Server', 'A')['session']
    send('A', 'open_loot', target=box['id'])
    wait(lambda: player('Server', 'A')['session'] > previous_session, 'blocked open evaluated by server')
    check('Opaque wall blocks interaction inside the distance limit', lambda: not player('Server', 'A')['inventory_open'] and len(loot('ConcurrentBox')['contents']['items']) == 2)
    send('Server', 'occluder', enabled=False)
    open_box('A', box['id'])
    check('Removing obstruction restores a valid session', lambda: player('A', 'A')['inventory_open'])
    close()

    # Capacity fixtures are setup only; all attempted changes still use production requests.
    place([('A', [1400, 4200, 500], 80, 0)])
    send('Server', 'inventory_fixture', target='EX_A', clear=True, items=[{'definition': 'Ammo', 'quantity': 60, 'count': 24}])
    sync()
    open_box('A', box['id'])
    before_items = items().copy()
    assert action('A', remaining, from_loot=True)
    check('Full backpack rejects transfer without partial mutation', lambda: items() == before_items and len(loot('ConcurrentBox')['contents']['items']) == 2)
    close()
    send('Server', 'inventory_fixture', target='EX_A', clear=True, items=[{'definition': 'AK', 'slot': 0}, {'definition': 'Ammo', 'quantity': 60, 'count': 2}, {'definition': 'Medkit', 'count': 2}])
    sync()
    ammo = next(i for i in items() if i['definition'] == 'Ammo')
    for invalid in [0, -1, 61, 2147483647]:
        assert action('A', ammo['id'], quantity=invalid, kind='drop')
    check('Invalid quantities never change ammunition', lambda: total('Ammo') == 120)
    assert action('A', ammo['id'], quantity=1, inventory_revision=0)
    check('Stale inventory revision rejected', lambda: total('Ammo') == 120)

    # Real inventory-backed reload, including interruption before its commit timer.
    gun = player('Server', 'A')['weapon_item']
    send('Server', 'ammo', target='EX_A', value=17)
    sync()
    send('A', 'pulse', action='Reload')
    check('Reload draws thirteen rounds from inventory', lambda: total('Ammo') == 107 and item(gun)['magazine'] == 30 and player('A', 'A').get('ammo') == 30)
    send('Server', 'ammo', target='EX_A', value=17)
    sync()
    send('A', 'pulse', action='Reload')
    check('Interruptible reload starts', lambda: player('Server', 'A')['combat_state'] == 1)
    assert not action('A', gun, kind='drop')
    time.sleep(3)
    check('Cancelled reload consumes no reserve rounds', lambda: total('Ammo') == 107)
    dropped = next(v for v in state('Server')['loot'] if any(i['id'] == gun for i in v['contents']['items']))
    send('A', 'open_loot', target=dropped['id'])
    check('Dropped partial magazine can be re-equipped', lambda: player('Server', 'A').get('ammo') == 17)

    place([('A', [1400, 4200, 500], 40, 0), ('B', [1400, 3900, 500], 80, 0)])
    send('A', 'medical')
    check('Medical use starts a server timer', lambda: player('Server', 'A')['medical_end'] > 0)
    active_med = next(i for i in items() if i['definition'] == 'Medkit')
    assert action('A', active_med['id'], kind='drop')
    check('Active medical item is reserved', lambda: total('Medkit') == 2)
    send('A', 'screenshot', name='medical-progress')
    check('Medical completion consumes one and heals exactly fifty', lambda: total('Medkit') == 1 and player('Server', 'A')['health'] == 90 and player('A', 'A')['health'] == 90)
    send('Server', 'inventory_fixture', target='EX_A', clear=False, items=[{'definition': 'Medkit', 'count': 2}])
    sync()
    before_meds = total('Medkit')
    send('A', 'medical')
    wait(lambda: player('Server', 'A')['medical_end'] > 0, 'cancel test started')
    send('A', 'cancel_medical')
    time.sleep(3.2)
    check('Cancelled medical action neither heals nor consumes', lambda: total('Medkit') == before_meds and player('Server', 'A')['health'] == 90)
    place([('A', [1400, 4200, 500], 100, 0)])
    send('A', 'medical')
    time.sleep(.4)
    check('Full health rejects use without consuming', lambda: total('Medkit') == before_meds and player('Server', 'A')['medical_end'] == 0)
    place([('A', [1400, 4200, 500], 60, 0)])
    send('A', 'medical')
    wait(lambda: player('Server', 'A')['medical_end'] > 0, 'damage interruption started')
    api['aim']('B', 'A')
    api['shot']('B', 'A', 20)
    time.sleep(3.2)
    check('Real hit cancels healing with no consumption', lambda: player('Server', 'A')['medical_end'] == 0 and player('Server', 'A')['health'] == 40 and total('Medkit') == before_meds)
    send('A', 'medical')
    wait(lambda: player('Server', 'A')['medical_end'] > 0, 'fire interruption started')
    send('A', 'aim', target='', head=False)
    send('A', 'pulse', action='Fire')
    check('Firing cancels medical action', lambda: player('Server', 'A')['medical_end'] == 0 and total('Medkit') == before_meds)

    for input_action in ['Jump', 'Sprint', 'Reload']:
        place([('A', [1400, 4200, 500], 40, 0)])
        send('A', 'medical')
        wait(lambda: player('Server', 'A')['medical_end'] > 0, input_action + ' interruption started')
        send('A', 'pulse', action=input_action)
        check(input_action + ' cancels medical use without consumption', lambda: player('Server', 'A')['medical_end'] == 0 and total('Medkit') == before_meds)
        time.sleep(3.2)
        check(input_action + ' cancellation cannot heal later', lambda: player('Server', 'A')['health'] == 40 and total('Medkit') == before_meds)
    send('Server', 'inventory_fixture', target='EX_A', clear=False, items=[{'definition': 'AK', 'slot': 1}])
    sync()
    secondary = next(i['id'] for i in items() if i['slot'] == 1)
    if api['args'].debug_hold:
        print('WAITING_FOR_DEBUGGER', api['OUT'] / 'Continue.txt', flush=True)
        wait(lambda: (api['OUT'] / 'Continue.txt').exists(), 'debugger continue marker')
    send('A', 'medical')
    wait(lambda: player('Server', 'A')['medical_end'] > 0, 'weapon switch interruption started')
    send('A', 'swap')
    check('Weapon switch cancels medical and commits equipment identities', lambda: player('Server', 'A')['medical_end'] == 0 and player('Server', 'A').get('weapon_item') == secondary and item(gun)['slot'] == 1)
    wait(lambda: player('Server', 'A')['combat_state'] == 0, 'first weapon switch finished')
    send('A', 'swap')
    check('Switching back restores original gun and correct slots', lambda: player('A', 'A').get('weapon_item') == gun and item(gun)['slot'] == 0)
    wait(lambda: player('Server', 'A')['combat_state'] == 0, 'second weapon switch finished')
    assert action('A', secondary, slot=0)
    check('Occupied weapon slot rejects replacement without dropping either gun', lambda: item(gun)['slot'] == 0 and item(secondary)['slot'] == 1)

    # A real lethal shot drops all item identities once; pawn destruction must not delete them.
    place([('A', [1400, 4200, 500], 20, 0)])
    sync()
    possessions = {i['id']: i.copy() for i in items()}
    send('A', 'medical')
    wait(lambda: player('Server', 'A')['medical_end'] > 0, 'death interruption started')
    api['aim']('B', 'A')
    api['shot']('B', 'A', 20)
    check('Death empties inventory and enters spectator', lambda: not items() and state('A').get('spectating') and not state('A').get('has_pawn'), 20)
    death_boxes = lambda: [v for v in state('Server')['loot'] if any(i['id'] in possessions for i in v['contents']['items'])]
    check('Exactly one death container owns all possessions', lambda: len(death_boxes()) == 1 and {i['id'] for i in death_boxes()[0]['contents']['items']} == set(possessions))
    check('Death preserves quantities and weapon magazine', lambda: all(i['quantity'] == possessions[i['id']]['quantity'] and i['magazine'] == possessions[i['id']]['magazine'] for i in death_boxes()[0]['contents']['items']))
    death = death_boxes()[0]
    position = death['position']
    place([('B', [position[0] - 60, position[1], position[2] + 90], 80, 0)])
    open_box('B', death['id'])
    wait(lambda: len(known('B')) == len(possessions), 'death container fully searched', 20)
    send('B', 'screenshot', name='death-container')
    assert not action('B', gun, from_loot=True, slot=1)
    check('Survivor loots the same weapon with unchanged magazine', lambda: item(gun, 'B') is not None and item(gun, 'B')['magazine'] == possessions[gun]['magazine'])
    close('B')
    send('A', 'leave')
    check('Dead player leaves without a second drop', lambda: state('A').get('map') == '/Game/Maps/Lobby' and len(death_boxes()) == 1)
    invariant('Death and subsequent leave preserve unique ownership')
    send('Server', 'end')
    check('Round returns survivor and server to lobby', lambda: state('B').get('map') == '/Game/Maps/Lobby' and state('Server').get('map') == '/Game/Maps/Lobby', 35)
    api['join']('A')
    api['begin_round']()
    check('Next round has fresh loadouts instead of previous loot', lambda: all(total('Ammo', r) == 120 and total('Medkit', r) == 2 for r in ['A', 'B']) and not any(v['label'] == 'ConcurrentBox' for v in state('Server')['loot']))
    invariant('Reopened match has no duplicated inventory identities')
