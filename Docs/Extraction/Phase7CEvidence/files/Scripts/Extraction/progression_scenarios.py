"""Three real raids through production economy RPCs, Slate, input, bullets and extraction."""
import concurrent.futures
import time


def run(g):
    send, state, player, wait, check = (g[k] for k in ['send', 'state', 'player', 'wait', 'check'])
    roles = g['ROLES']
    peers = ['Server'] + roles

    def account(r): return state(r).get('account', {})
    def progression(r): return account(r).get('progression', {})
    def quest(r, n): return progression(r)['quests'][n]
    def count(r, definition, side='stash'):
        return sum(i['quantity'] for i in account(r).get(side, {}).get('items', []) if i['definition'] == definition)
    def items(r): return player('Server', r)['inventory']['items']
    def carried(r, definition): return sum(i['quantity'] for i in items(r) if i['definition'] == definition)
    def room(peer='Server'): return state(peer).get('key_rooms', [{}])[0]
    def place(name, point, health=100, yaw=90):
        send('Server', 'fixture', players=[{'name': name, 'position': point, 'health': health, 'shield': 0, 'clear_trauma': True, 'yaw': yaw}])
    def move_item(r, item, **kwargs):
        before = account(r)['revision']
        send(r, 'storage', item=item['id'], quantity=item['quantity'], **kwargs)
        wait(lambda: account(r)['revision'] > before, 'storage transaction')
    def economy(r, kind='buy', **kwargs):
        old = account(r)['revision']
        send(r, 'economy', kind=kind, **kwargs)
        wait(lambda: account(r)['revision'] > old, r+' economy commit '+kind)
    def click(r, x, y):
        send(r, 'mouse', x=x, y=y, action='down')
        send(r, 'mouse', x=x, y=y, action='up')
    def close(r): send(r, 'key', key='Escape')
    def ready_scene():
        g['begin_round']()
        if g['args'].lag:
            for peer in peers: send(peer, 'lag')
            check('Round retains 50ms each direction and two percent loss', lambda: all(
                state(peer).get('packet_lag') == 50 and state(peer).get('packet_loss') == 2 for peer in peers))
        check('Room starts locked with authoritative barrier on every peer', lambda: all(room(p).get('blocked') is True and room(p).get('open') is False for p in peers))
        check('Four live guards and original patrol paths present', lambda: len(state('Server').get('guards', [])) == 4
              and all(p['valid'] for p in state('Server')['nav_paths']))
        check('Each guard reaches a patrol point', lambda: all(a.get('arrivals', 0) >= 1 for a in state('Server')['guards']), 100)
        for i, guard in enumerate(state('Server')['guards']): place(guard['name'], [-4300+i*180, -6300, 180])
        for r in roles: send(r, 'aim', target='', head=False)
    def extract_all():
        centre = state('Server')['exit_position']
        for i, r in enumerate(roles):
            send(r, 'aim', target='', head=False)
            send(r, 'inventory', open=False)
            place('EX_'+r, [centre[0]+(i%3-1)*130, centre[1]+(i//3)*130, centre[2]+90])
        check('Every participant independently extracts', lambda: all(account(r).get('outcome') == 1 for r in roles), 25)
        send('A', 'screenshot', name='progression-settlement')
        check('Returns to unlocked lobby', lambda: all(state(r).get('map') == '/Game/Maps/Lobby' for r in peers)
              and all(account(r).get('locked') is False and account(r).get('bound')
                      and len(progression(r).get('quests', [])) == 3 for r in roles), 45)
    def collect_file(r):
        cache = room()['cache']
        point = room()['cache_position']
        place('EX_'+r, [point[0], point[1]-100, point[2]+70])
        send(r, 'open_loot', target=cache)
        wait(lambda: any(i['definition'] == 'Document' for i in player(r, r).get('loot_view', {}).get('items', [])), 'File actually searched', 25)
        file = next(i for i in player(r, r)['loot_view']['items'] if i['definition'] == 'Document')
        wait(lambda: player(r, r)['inventory'] == player('Server', r)['inventory'], 'inventory synced before pickup')
        send(r, 'screenshot', name='key-room-search')
        send(r, 'item', item=file['id'], from_loot=True)
        check('Searched file transferred once', lambda: any(i['id'] == file['id'] for i in items(r)))
        send(r, 'inventory', open=False)
        return file['id']

    for r in roles:
        check(r+' has isolated new progression', lambda r=r: account(r).get('bound') and progression(r).get('credits') == 2000 and progression(r).get('level') == 1)
    send('A', 'progression_ui', tasks=True)
    time.sleep(.4)
    click('A', 1080, 207)
    check('Task accepted through actual Slate action', lambda: quest('A', 0)['accepted'])
    send('A', 'screenshot', name='tasks-first')
    close('A')
    for r in roles[1:]: economy(r, 'accept', quest=0)

    send('A', 'progression_ui')
    time.sleep(.4)
    click('A', 140, 488)  # key, eighth catalog row
    send('A', 'key', key='Up')
    click('A', 220, 600)
    send('A', 'screenshot', name='shop-key-quote')
    click('A', 675, 450)
    check('Slate purchase debits two keys atomically', lambda: count('A', 'Key') == 2 and progression('A')['credits'] == 1400)
    click('A', 760, 187)  # first stored rifle, still loaded
    click('A', 220, 600)
    send('A', 'screenshot', name='shop-whole-rifle')
    click('A', 675, 450)
    check('Whole rifle sale includes its thirty rounds', lambda: progression('A')['credits'] == 2030 and count('A', 'AK') == 2)
    close('A')
    economy('A', definition='Bandage', save='bandage')
    before = account('A')['revision']
    send('A', 'economy', replay='bandage')
    time.sleep(.7)
    check('Exact transaction replay is harmless', lambda: account('A')['revision'] == before and count('A', 'Bandage') == 1)
    send('A', 'economy', definition='AmmoAP')
    time.sleep(.7)
    check('Locked merchandise rejected by authority', lambda: account('A')['revision'] == before and count('A', 'AmmoAP') == 0)
    economy('A', definition='Ammo', quantity=1)
    before = account('A')['revision']
    send('A', 'economy', replay='bandage', revision=before)
    time.sleep(.7)
    check('Old operation with changed revision cannot replay', lambda: account('A')['revision'] == before and count('A', 'Bandage') == 1)
    for i in [i for i in account('A')['stash']['items'] if i['definition'] == 'Key']: move_item('A', i)
    send('A', 'ready')
    wait(lambda: account('A')['locked'], 'ready locks services')
    send('A', 'economy', definition='Ammo')
    time.sleep(.6)
    check('Ready player cannot trade', lambda: account('A')['revision'] == before+2)
    send('A', 'ready')
    wait(lambda: not account('A')['locked'], 'ready cancellation')

    ready_scene()
    check('Key room navigation connection exists', lambda: room().get('nav_valid') is True)
    check('Room recipe has three separate files, four electronics and two tools', lambda: any(
        c['id'] == room()['cache']
        and sum(i['quantity'] for i in c['contents']['items'] if i['definition'] == 'Document') == 3
        and len([i for i in c['contents']['items'] if i['definition'] == 'Document']) == 3
        and sum(i['quantity'] for i in c['contents']['items'] if i['definition'] == 'Electronics') == 4
        and sum(i['quantity'] for i in c['contents']['items'] if i['definition'] == 'Tools') == 2
        for c in state('Server')['loot']))
    send('A', 'unlock')
    time.sleep(.7)
    check('Remote door request cannot consume a key', lambda: not room()['unlocked'] and carried('A', 'Key') == 2)
    approach = room()['approach']
    place('EX_B', approach)
    time.sleep(.7)
    send('B', 'unlock')
    time.sleep(.7)
    check('Missing key rejected', lambda: not room()['unlocked'] and carried('A', 'Key') == 2)
    cache_point = room()['cache_position']
    place('EX_B', [cache_point[0], cache_point[1]-100, cache_point[2]+70])
    send('B', 'open_loot', target=room()['cache'])
    time.sleep(.7)
    check('Direct container request cannot bypass closed door', lambda: not player('Server', 'B')['inventory_open'] and carried('B', 'Document') == 0)
    place('EX_A', [approach[0]-50, approach[1], approach[2]])
    place('EX_B', [approach[0]+50, approach[1], approach[2]])
    time.sleep(.6)
    send('A', 'unlock', revision=-1)
    time.sleep(.6)
    check('Stale inventory revision cannot consume key', lambda: not room()['unlocked'] and carried('A','Key') == 2)
    send('A', 'inventory', open=True)
    wait(lambda: player('Server','A')['inventory_open'], 'Inventory open before conflicting action')
    send('A', 'unlock')
    time.sleep(.6)
    check('Conflicting inventory action cannot open room', lambda: not room()['unlocked'] and carried('A','Key') == 2)
    send('A', 'inventory', open=False)
    key = next(i for i in items('A') if i['definition'] == 'Key')
    send('A', 'item', item=key['id'], kind='drop')
    wait(lambda: carried('A', 'Key') == 1, 'key dropped')
    loose = next(c for c in state('Server')['loot'] if any(i['id'] == key['id'] for i in c['contents']['items']))
    send('B', 'open_loot', target=loose['id'])
    check('Key identity can be transferred through normal pickup', lambda: carried('B', 'Key') == 1)
    place('EX_B', [approach[0]+240, approach[1], approach[2]])
    place('EX_A', approach)
    send('A', 'look', yaw=90)
    send('A', 'pulse', action='Move', duration=.7)
    time.sleep(1.2)
    check('Closed door physically blocks normal movement', lambda: player('Server', 'A')['position'][1] < room()['position'][1]-30)
    place('EX_A', [approach[0], approach[1]-150, approach[2]])
    send('A', 'look', yaw=90, pitch=-7)
    send('A', 'screenshot', name='key-room-closed')
    place('EX_A', [approach[0]-50, approach[1], approach[2]])
    place('EX_B', [approach[0]+50, approach[1], approach[2]])
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(lambda r: send(r, 'unlock'), ['A', 'B']))
    check('Competing opens consume exactly one key', lambda: all(room(p).get('open') for p in peers)
          and carried('A', 'Key') + carried('B', 'Key') == 1)
    check('Open door removes its actual blocking trace', lambda: all(room(p).get('blocked') is False for p in peers))
    send('A', 'unlock')
    time.sleep(.6)
    check('Duplicate opening never consumes remaining key', lambda: carried('A', 'Key') + carried('B', 'Key') == 1)
    if state('A').get('region'):
        cache_id = room()['cache']
        cache_contents = next(c['contents'] for c in state('Server')['loot'] if c['id'] == cache_id)
        quest_before = progression('A')
        place('EX_A', [-1500, -300, 460])
        check('Key-room owner unloads north from the old interior', lambda: not state('A')['region']['loaded'], 45)
        place('EX_A', approach)
        check('Key-room owner reloads north on return', lambda: state('A')['region']['visible'])
        check('Streaming preserves opened door, remaining key, cache and quest state', lambda:
              all(room(p).get('open') and not room(p).get('blocked') for p in peers)
              and carried('A', 'Key') + carried('B', 'Key') == 1
              and room()['cache'] == cache_id
              and next(c['contents'] for c in state('Server')['loot'] if c['id'] == cache_id) == cache_contents
              and progression('A') == quest_before)
    place('EX_B', [approach[0]+240, approach[1], approach[2]])
    place('EX_A', [approach[0], approach[1]-150, approach[2]])
    send('A', 'look', yaw=90, pitch=-7)
    send('A', 'screenshot', name='key-room-open')
    place('EX_A', approach)
    send('A', 'look', yaw=90)
    send('A', 'pulse', action='Move', duration=.9)
    check('Normal movement traverses open doorway', lambda: player('Server', 'A')['position'][1] > room()['position'][1]+80)
    for label, point, yaw, axis, limit, direction in [
        ('right', [4010,4600,452], 0, 0, 4080, 1),
        ('back', [3800,4810,452], 90, 1, 4880, 1),
        ('left', [3590,4600,452], 180, 0, 3520, -1),
    ]:
        place('EX_A', point)
        send('A', 'look', yaw=yaw)
        send('A', 'pulse', action='Move', duration=.5)
        time.sleep(.9)
        check('Solid '+label+' wall blocks normal movement', lambda axis=axis, limit=limit, direction=direction:
              player('Server', 'A')['position'][axis]*direction < limit*direction)
    old_file = collect_file('A')
    collect_file('B')
    check('Opened room is accessible to both players regardless of key consumer', lambda: carried('A','Document') == 1 and carried('B','Document') == 1)
    send('A', 'screenshot', name='key-room-loot')
    extract_all()
    check('First task complete for every successful participant', lambda: all(quest(r, 0)['progress'] == 1 for r in roles))
    check('File obtained before final quest is not eligible', lambda: quest('A', 2)['eligible_files'] == 0)
    for r in roles: economy(r, 'claim', quest=0)
    economy('A', 'accept', quest=1)
    check('First reward unlocks second-level items', lambda: all(progression(r)['level'] == 2 for r in roles))
    economy('A', definition='RedDot')

    ready_scene()
    guards = [a['name'] for a in state('Server')['guards']]
    for name in guards[:2]:
        send('Server', 'ammo', target=name, value=0)
        place(name, [1400, 4200, 500], 20)
        place('EX_A', [1400, 3900, 500])
        send('A', 'aim', target=name, head=False)
        time.sleep(.5)
        before = player('Server', 'A')['guard_kills']
        for _ in range(12):
            send('A', 'pulse', action='Fire')
            time.sleep(.4)
            if player('Server', 'A')['guard_kills'] > before: break
        check('Real bullet kill credited to shooter once', lambda: player('Server', 'A')['guard_kills'] == before+1)
    check('Peer receives no shared kill progress', lambda: player('Server', 'B')['guard_kills'] == 0)
    check('Kill counter replicates only to its owner', lambda: player('A','A')['guard_kills'] == 2
          and all(player(peer,'A')['guard_kills'] == 0 for peer in roles if peer != 'A'))
    check('Live kills do not yet change durable task progress', lambda: quest('A', 1)['progress'] == 0)
    extract_all()
    check('Successful extraction commits both kills', lambda: quest('A', 1)['progress'] == 2 and quest('B', 1)['progress'] == 0)
    economy('A', 'claim', quest=1)
    economy('A', 'accept', quest=2)
    check('Second reward unlocks level three', lambda: progression('A')['level'] == 3 and progression('B')['level'] == 2)
    before = account('A')['revision']
    send('A', 'economy', kind='deliver', quest=2)
    time.sleep(.7)
    check('Pre-acceptance file cannot be submitted', lambda: account('A')['revision'] == before and any(i['id'] == old_file for i in account('A')['stash']['items']))
    economy('A', definition='AmmoAP', quantity=10)
    economy('A', definition='Key')
    move_item('A', next(i for i in account('A')['stash']['items'] if i['definition'] == 'Key'))

    ready_scene()
    place('EX_A', room()['approach'])
    time.sleep(.7)
    send('A', 'pulse', action='Equip')
    check('Actual interact input opens the new round door', lambda: room()['open'])
    file = collect_file('A')
    check('Live document is not yet eligible for delivery', lambda: quest('A', 2)['eligible_files'] == 0)
    extract_all()
    check('Qualified extraction saves document eligibility', lambda: quest('A', 2)['eligible_files'] == 1)
    send('A', 'progression_ui', tasks=True)
    time.sleep(.5)
    click('A', 1080, 507)
    check('Slate file submission consumes only qualified file', lambda: quest('A', 2)['delivered'] and not any(i['id'] == file for i in account('A')['stash']['items'])
          and any(i['id'] == old_file for i in account('A')['stash']['items']))
    click('A', 1080, 507)
    check('Final reward claimed through UI', lambda: quest('A', 2)['claimed'] and progression('A')['experience'] == 600)
    send('A', 'screenshot', name='tasks-complete')
    before = progression('A').copy()
    click('A', 1080, 507)
    time.sleep(.7)
    check('Repeated reward click grants nothing', lambda: progression('A') == before)
    close('A')
    g['report']['progression_final'] = {r: progression(r) for r in roles}
    g['save']()
