"""Four real AI patrols and north combat/loot under six-client transport load."""
import time


def run(g):
    send, check, wait, state, player = (g[n] for n in ['send', 'check', 'wait', 'state', 'player'])
    roles = g['ROLES']
    assert len(roles) == 6
    for role in ['Server', 'A']:
        assert not send(role, 'profile', start=True, name='north-combat')
    g['report']['profile_boundary'] = 'Functional six-player/four-AI stress sample with 30 fps cap; separate from uncapped 1080p medium environment acceptance.'

    def place(name, position, health=100, yaw=0):
        assert not send('Server', 'fixture', players=[{'name': name, 'position': position,
                        'health': health, 'shield': 0, 'clear_trauma': True, 'yaw': yaw}])

    def guard(name):
        return next((v for v in state('Server')['guards'] if v['name'] == name), {})

    def preload(role):
        place('EX_' + role, [1400, 5000, 460])
        check(role + ' north region is ready for combat fixture', lambda: state(role)['region']['visible'])

    check('Six humans and four AI are independent', lambda: len(state('Server')['players']) == 6 and
          all(len(state(p)['guards']) == 4 for p in g['processes']))
    check('All ten directed patrol legs have complete navigation', lambda: len(state('Server')['nav_paths']) == 10 and all(p['valid'] for p in state('Server')['nav_paths']))
    check('Every guard reaches start, destination and return including north crossing', lambda: all(a.get('arrivals', 0) >= 3 for a in state('Server')['guards']), 160)
    guards = [v['name'] for v in state('Server')['guards']]
    for i, name in enumerate(guards):
        place(name, [-4300 + i * 150, -6300, 180])
    target = guards[-1]
    preload('A')
    # Stay on the audited walking corridor. The former x=1400 sight line at
    # y=9000..9500 crossed source geometry (LLDB: LineOfSightTo bHit=true).
    place('EX_A', [1750, 8800, 154])
    place(target, [1750, 8400, 154], yaw=90)
    wait(lambda: player('Server', 'A')['health'] < 100 and guard(target)['shots'] > 0, 'North AI lands an authoritative bullet', 15)
    first_hit = player('Server', 'A').copy()
    send('Server', 'ammo', target=target, value=0)
    place('EX_A', [1750, 8150, 154])
    place(target, [1750, 8400, 154], health=20, yaw=90)
    gun = next(i for i in guard(target)['inventory']['items'] if i['definition'] == 'AK')
    send('A', 'aim', target=target, head=False)
    for _ in range(10):
        send('A', 'pulse', action='Fire')
        time.sleep(.4)
        if guard(target).get('dead'):
            break
    check('North guard dies from normal player bullets', lambda: guard(target).get('dead') and first_hit['health'] < 100)
    box = next(b for b in state('Server')['loot'] if any(i['id'] == gun['id'] for i in b['contents']['items']))
    place('EX_A', [box['position'][0] + 100, box['position'][1], 154])
    send('A', 'aim', target='', head=False)
    send('A', 'open_loot', target=box['id'])
    wait(lambda: any(i['id'] == gun['id'] for i in player('A', 'A').get('loot_view', {}).get('items', [])), 'Search reveals original north AI rifle', 15)
    send('A', 'item', item=gun['id'], from_loot=True, quantity=1, slot=1)
    check('North AI rifle enters the original player inventory once', lambda: any(i['id'] == gun['id'] and i['slot'] == 1 for i in player('Server', 'A')['inventory']['items']))
    send('A', 'inventory', open=False)
    remains = next(b for b in state('Server')['loot'] if b['id'] == box['id'])['contents']
    place('EX_A', [-1500, -300, 460])
    check('North combat client unloads on return to old room', lambda: not state('A')['region']['loaded'], 45)
    preload('A')
    check('AI death container persists across client unload and reload', lambda: next(b for b in state('Server')['loot'] if b['id'] == box['id'])['contents'] == remains)
    # A is the rendered client. NullRHI peers have no screen crosshair and must
    # not be used as input-driven shooters (TraceUnderCrosshairs returns early).
    preload('B')
    place('EX_B', [1750, 8400, 154], health=20)
    place('EX_A', [1750, 8150, 154])
    original = player('Server', 'B')['weapon_item']
    send('A', 'aim', target='EX_B', head=False)
    time.sleep(.8)
    for _ in range(16):
        send('A', 'pulse', action='Fire')
        time.sleep(.4)
        if player('Server', 'B').get('life') == 1:
            break
    check('North PVP down and execution produce a terminal death', lambda: player('Server', 'B').get('life') == 1)
    check('Northern player death preserves exactly one original weapon', lambda: len([i for b in state('Server')['loot'] for i in b['contents']['items'] if i['id'] == original]) == 1)
    send('A', 'aim', target='', head=False)
    exit_at = state('Server')['exit_position']
    place('EX_A', [exit_at[0], exit_at[1], exit_at[2] + 90])
    check('North AI rifle survives extraction and persistence', lambda: state('A')['account']['outcome'] == 1 and any(i['id'] == gun['id'] for i in state('A')['account']['stash']['items']), 22)
    preload('C')
    place('EX_C', [1750, 8800, 154], health=20)
    check('Extracted ally sees a ready northern spectator view', lambda: state('A').get('spectated') == 'EX_C' and state('A')['region']['visible'] and state('A')['view_position'][1] > 6000)
    send('A', 'screenshot', name='north-ai-export-spectator')
    place('EX_C', [-1500, -300, 460])
    check('Spectator leaves north with its returning ally', lambda: state('A').get('spectated') == 'EX_C' and not state('A')['region']['loaded'], 45)
    check('North death loot remains while all local views are unloaded', lambda: len([i for b in state('Server')['loot'] for i in b['contents']['items'] if i['id'] == original]) == 1)
    send('B', 'leave')
    check('Northern death spectator can leave the session', lambda: state('B').get('map') == '/Game/Maps/Lobby' and not state('B').get('session'))
    for role in ['A', 'Server']:
        assert not send(role, 'profile', start=False)
    wait(lambda: all((g['OUT'] / (role + '-north-combat.csv')).exists() and any(
        'Capture Ended. Writing CSV to file :' in line and role + '-north-combat.csv' in line
        for line in (g['OUT'] / (role + '.log')).read_text(encoding='utf-8', errors='replace').splitlines())
        for role in ['A', 'Server']), 'North combat CSV saved', 45)
    send('Server', 'end')
    check('North combat round releases server region and guards on lobby return', lambda: state('Server').get('map') == '/Game/Maps/Lobby' and not state('Server').get('guards') and 'region' not in state('Server'), 40)
    g['report']['north_ai_export'] = {'item': gun['id'], 'profile': state('A')['account']['profile'], 'first_hit': first_hit}
