"""Network acceptance through production requests; fixtures arrange only position/vitals."""
import time


def lobby(env):
    g = env
    check, send, player, state = (g[x] for x in ['check', 'send', 'player', 'state'])
    roles = g['ROLES']
    check('Extraction auto assignment is balanced and capped', lambda: all(
        player('Server', r).get('squad') == (1 if i % 2 == 0 else 2) for i, r in enumerate(roles)))
    if len(roles) == 3:
        send('C', 'squad', value=2)
        check('Unready player can change squad', lambda: player('Server', 'C').get('squad') == 2)
        send('C', 'squad', value=1)
        check('Squad change reaches all clients', lambda: all(player(r, 'C').get('squad') == 1 for r in roles))
    send('A', 'squad', value=0)
    time.sleep(.6)
    check('Invalid squad rejected', lambda: player('Server', 'A').get('squad') == 1)
    if len(roles) == 6:
        send('A', 'squad', value=2)
        time.sleep(.6)
        check('Full squad rejects fourth member', lambda: player('Server', 'A').get('squad') == 1)
        if 'G' not in g['report']['processes']:
            g['launch']('G')
            send('G', 'direct_join', expect_rejection=True, reason='ROOM_FULL')
            check('Seventh client is rejected by lobby authority', lambda: 'ROOM_FULL' in (g['OUT']/'G.log').read_text(encoding='utf-8', errors='replace'), 35)
            check('Rejected client never occupies a squad or raid slot', lambda: len(state('Server')['players']) == 6)
            g['finishing'].add('G')
            send('G', 'finish')
            proc = g['processes'].pop('G')
            code = proc.wait(timeout=40)
            g['report']['processes']['G']['exit_code'] = code
            assert code == 0
    send('A', 'ready')
    check('A readied through lobby UI', lambda: player('Server', 'A').get('ready'))
    send('A', 'squad', value=2)
    time.sleep(.6)
    check('Ready player cannot switch squad', lambda: player('Server', 'A').get('squad') == 1)
    send('A', 'ready')
    check('Ready cancellation unlocks player', lambda: not player('Server', 'A').get('ready'))
    send('A', 'screenshot', name='squad-lobby')


def run(env):
    g = env
    send, check, wait, state, player, aim = (g[x] for x in ['send', 'check', 'wait', 'state', 'player', 'aim'])
    roles = g['ROLES']
    peers = ['Server'] + roles
    if g['args'].scenario == 'lifecycle':
        lifecycles(g)
        return
    if g['args'].scenario == 'pvpve':
        pvpve(g)
        return

    def place(values):
        send('Server', 'fixture', players=[{'name': 'EX_'+r, 'position': p, 'health': h, 'shield': 0, 'clear_trauma': True}
                                         for r, (p, h) in values.items()])
        time.sleep(.7)

    positions = {'A': [1400, 4200, 500], 'B': [1400, 3550, 500], 'C': [1550, 4200, 500],
                 'D': [1800, 3900, 500], 'E': [1800, 4500, 500], 'F': [2200, 4500, 500]}

    def hurt(target, life):
        aim('B', target)
        for _ in range(10):
            send('B', 'pulse', action='Fire')
            time.sleep(.55)
            if player('Server', target).get('life') == life:
                break
        check(target+' reaches life state '+str(life)+' through actual shots', lambda: player('Server', target).get('life') == life)

    check('Squads survive seamless travel', lambda: all(player('Server', r).get('squad') == (1 if i % 2 == 0 else 2) for i, r in enumerate(roles)))

    if g['args'].scenario == 'pvpve':
        check('Four guards replicate without occupying player seats', lambda: all(len(state(r).get('guards', [])) == 4 for r in peers)
              and len(state('Server').get('players', [])) == 6)
        check('Guards navigate or engage on the live map', lambda: any(b.get('arrivals', 0) > 0 or b.get('shots', 0) > 0 for b in state('Server')['guards']), 45)
        for r in roles:
            send(r, 'screenshot', name='pvpve')
        # The extended AI lifecycle and extraction scenario follows the common mechanics checks.

    place({r: (positions[r], 80) for r in roles})
    aim('C', 'A')
    before = player('Server', 'C')['accepted']
    send('C', 'pulse', action='Fire')
    check('Friendly shot is authoritative', lambda: player('Server', 'C')['accepted'] == before + 1)
    time.sleep(.5)
    check('Friendly shots do not reduce health', lambda: all(player(r, 'A').get('health') == 80 for r in peers))
    send('C', 'ability', kind='heal', target='EX_B')
    time.sleep(.5)
    check('Cannot heal enemy', lambda: player('Server', 'C').get('action') == 0 and player('Server', 'C').get('heal_ready') == 0)
    # The real Z binding heals the aimed ally. Clear that sight line for the
    # self-heal case instead of accidentally aiming at C standing beside A.
    place({'A': ([1400, 4800, 500], 40)})
    send('A', 'input_key', key='Z', pressed=True)
    send('A', 'input_key', key='Z', pressed=False)
    check('Z input starts an authoritative self heal channel', lambda: player('Server', 'A').get('action') == 2)
    send('A', 'ability', kind='duplicate')
    check('Self heal completes once for 50 HP', lambda: all(player(r, 'A').get('health') == 90 for r in peers))
    cooldown = player('Server', 'A')['heal_ready']
    send('A', 'ability', kind='heal')
    time.sleep(.6)
    check('Heal cooldown and duplicate cannot grant extra healing', lambda: player('Server', 'A')['health'] == 90
          and player('Server', 'A')['action'] == 0 and player('Server', 'A')['heal_ready'] == cooldown)
    place({'A': (positions['A'], 20)})
    send('C', 'ability', kind='heal', target='EX_A')
    check('Ally heal succeeds and replicates', lambda: all(player(r, 'A').get('health') == 70 for r in peers))
    place({'B': (positions['B'], 40)})
    send('B', 'ability', kind='heal')
    check('Interruptible heal begins', lambda: player('Server', 'B').get('action') == 2)
    send('B', 'pulse', action='Move', duration=.6)
    check('Movement cancels healing without cooldown', lambda: player('Server', 'B')['action'] == 0
          and player('Server', 'B')['health'] == 40 and player('Server', 'B')['heal_ready'] == 0)
    place({'B': (positions['B'], 40)})
    send('B', 'ability', kind='heal')
    check('Inventory interruption channel starts', lambda: player('Server', 'B')['action'] == 2)
    send('B', 'inventory', open=True)
    check('Inventory cancels channel', lambda: player('Server', 'B')['action'] == 0 and player('Server', 'B')['heal_ready'] == 0)
    send('B', 'inventory', open=False)

    # C remains beside A; the hostile B can actually shoot A without a friendly body in the way.
    place({'A': (positions['A'], 20), 'B': (positions['B'], 100), 'C': (positions['C'], 80)})
    inventory = player('Server', 'A')['inventory']
    old_drops = len(state('Server')['loot'])
    if g['args'].scenario == 'squad':
        send('A','input_key',key='Three',pressed=True)
        send('A','input_key',key='Three',pressed=False)
        check('Knife selected before downing',lambda:all(player(r,'A').get('knife_active') for r in peers))
    hurt('A', 5)
    check('Downed players retain their inventory and do not drop', lambda: player('Server', 'A')['inventory'] == inventory and len(state('Server')['loot']) == old_drops)
    check('Downed state replicates to every peer', lambda: all(player(r, 'A').get('life') == 5 for r in peers))
    if g['args'].scenario == 'squad':
        check('Downing clears knife on all peers',lambda:all(not player(r,'A').get('knife_active') for r in peers))
        for _ in range(2):send('A','pulse',action='ToggleCameraView',duration=0)
        send('A','screenshot',name='knife-downed')
    accepted = player('Server', 'A')['accepted']
    send('A', 'pulse', action='Fire')
    send('A', 'ability', kind='heal')
    time.sleep(.6)
    check('Downed cannot fire or heal', lambda: player('Server', 'A')['accepted'] == accepted and player('Server', 'A')['action'] == 0)
    aim('C', 'A')
    send('C', 'input_key', key='E', pressed=True)
    check('Holding E owns the rescue channel', lambda: player('Server', 'C')['action'] == 1 and player('Server', 'A')['rescuer'] == 'EX_C')
    if len(roles) == 6:
        place({'E': ([1250, 4200, 500], 80)})
        send('E', 'ability', kind='revive', target='EX_A')
        time.sleep(.5)
        check('Second rescuer cannot steal the active rescue', lambda: player('Server', 'E')['action'] == 0 and player('Server', 'A')['rescuer'] == 'EX_C')
    send('C', 'input_key', key='E', pressed=False)
    check('Releasing E cancels rescue and frees target', lambda: player('Server', 'C')['action'] == 0 and player('Server', 'A')['rescuer'] == '')
    send('C', 'ability', kind='revive', target='EX_A')
    check('Second rescue begins', lambda: player('Server', 'C')['action'] == 1)
    send('C', 'pulse', action='Move', duration=.4)
    check('Movement cancels rescue', lambda: player('Server', 'C')['action'] == 0 and player('Server', 'A')['life'] == 5)
    place({'C': (positions['C'], 80)})
    send('Server', 'occluder', enabled=True)
    send('C', 'ability', kind='revive', target='EX_A')
    time.sleep(.5)
    check('Wall blocks rescue', lambda: player('Server', 'C')['action'] == 0 and player('Server', 'A')['life'] == 5)
    send('Server', 'occluder', enabled=False)
    send('B', 'ability', kind='revive', target='EX_A')
    time.sleep(.5)
    check('Enemy cannot rescue a downed player', lambda: player('Server', 'B')['action'] == 0)
    send('C', 'ability', kind='revive', target='EX_A')
    send('A', 'screenshot', name='being-rescued')
    check('Rescue restores 30 HP and zero shield', lambda: all(player(r, 'A').get('life') == 0 and player(r, 'A').get('health') == 30 and player(r, 'A').get('shield') == 0 for r in peers), 12)
    check('Rescue clears deadline without changing inventory', lambda: player('Server', 'A')['bleed_at'] == 0 and player('Server', 'A')['inventory'] == inventory)
    if g['args'].scenario == 'squad':
        send('A','input_key',key='Three',pressed=True)
        send('A','input_key',key='Three',pressed=False)
        check('Revived owner can reselect a bound knife',lambda:all(player(r,'A').get('knife_active') for r in peers) and player('A','A').get('knife_view_attached'))
        send('A','screenshot',name='knife-revived')
        send('A','input_key',key='One',pressed=True)
        send('A','input_key',key='One',pressed=False)

    send('A', 'aim', target='', head=False)
    send('Server', 'occluder', enabled=True)
    send('A', 'input_key', key='X', pressed=True)
    send('A', 'input_key', key='X', pressed=False)
    check('X input creates one smoke actor on every peer', lambda: all(len(state(r).get('smokes', [])) == 1 for r in peers))
    expected_smoke = state('Server')['smokes'][0]
    check('Smoke deployment is clipped before the opaque wall', lambda: 1400 < expected_smoke['position'][0] < 1445 and abs(expected_smoke['position'][1]-4200) < 30)
    send('Server', 'occluder', enabled=False)
    check('Smoke radius and server expiry agree', lambda: expected_smoke['radius'] == 400 and all(len(state(r)['smokes']) == 1 and abs(state(r)['smokes'][0]['expires'] - expected_smoke['expires']) < .001 and all(abs(a-b) < .2 for a,b in zip(state(r)['smokes'][0]['position'], expected_smoke['position'])) for r in peers))
    send('A', 'ability', kind='smoke')
    time.sleep(.7)
    check('Smoke cooldown prevents duplicate actors', lambda: all(len(state(r)['smokes']) == 1 for r in peers))
    centre = expected_smoke['position']
    place({'C': ([centre[0], centre[1], 500], 80)})
    place({'A': ([centre[0]-700, centre[1], 500], 30)})
    aim('A', 'C')
    send('A', 'screenshot', name='smoke-outside')
    send('C', 'screenshot', name='smoke-inside')
    check('Smoke expires on every client', lambda: all(len(state(r)['smokes']) == 0 for r in peers), 25)

    if g['args'].scenario == 'squad':
        place({'A': (positions['A'], 20), 'C': (positions['C'], 20), 'B': (positions['B'], 100)})
        send('A','input_key',key='Three',pressed=True)
        send('A','input_key',key='Three',pressed=False)
        check('Knife selected before squad wipe',lambda:player('Server','A').get('knife_active'))
        hurt('A', 5)
        hurt('C', 5 if len(roles) == 6 else 1)
        if len(roles) == 6:
            place({'E': ([1550, 4500, 500], 20)})
            hurt('E', 1)
        check('Squad wipe finalizes downed teammate', lambda: player('Server', 'A').get('life') == 1)
        check('Death leaves no active knife on any peer',lambda:all(not player(r,'A').get('knife_active') for r in peers))
        send('A','screenshot',name='knife-dead')
        check('Wiped squad cannot spectate the enemy', lambda: state('A').get('spectating') and state('C').get('spectating')
              and state('A').get('spectated') == '' and state('C').get('spectated') == '', 15)
    send('Server', 'end')
    check('Returns to lobby with ready reset and squad retained', lambda: all(state(r).get('map') == '/Game/Maps/Lobby' for r in peers)
          and all(not player('Server', r).get('ready') and player('Server', r).get('squad') == (1 if i % 2 == 0 else 2) for i, r in enumerate(roles)), 40)
    send('A', 'screenshot', name='phase5-return')


def pvpve(g):
    send, check, wait, state, player = (g[x] for x in ['send', 'check', 'wait', 'state', 'player'])
    peers = ['Server'] + g['ROLES']

    def place(name, position, health=100, shield=0, yaw=None):
        entry = {'name': name, 'position': position, 'health': health, 'shield': shield, 'clear_trauma': True}
        if yaw is not None:
            entry['yaw'] = yaw
        send('Server', 'fixture', players=[entry])

    def guard(name):
        return next((a for a in state('Server').get('guards', []) if a['name'] == name), {})

    def aim_at(shooter, name):
        send(shooter, 'aim', target=name, head=False)
        time.sleep(.3)

    check(str(len(g['ROLES']))+' humans and four independent AI replicate', lambda: len(state('Server')['players']) == len(g['ROLES'])
          and all(len(state(r).get('guards', [])) == 4 for r in peers))
    check('AI creates no human persistent profiles', lambda: len(state('Server')['stored_results']) == len(g['ROLES']))
    check('Every patrol leg has a complete cooked navigation path', lambda: len(state('Server').get('nav_paths', [])) == 10
          and all(p['valid'] for p in state('Server')['nav_paths']))
    check('All four guards actually reach patrol points', lambda: all(a.get('arrivals', 0) >= 2 for a in state('Server')['guards']), 100)
    guards = [a['name'] for a in state('Server')['guards']]
    target = guards[0]
    # Isolate the combat sight line after actual navigation. No AI state/damage bypass.
    for i, name in enumerate(guards[1:]):
        place(name, [-4300 + i*200, -6300, 180])
    send('Server', 'occluder', enabled=True)
    place('EX_A', [1800, 4200, 500], shield=100)
    blocked_shots = guard(target)['shots']
    for _ in range(5):
        place(target, [1400, 4200, 500], yaw=0)
        time.sleep(.2)
    check('Opaque wall prevents guard acquisition and shooting', lambda: guard(target)['shots'] == blocked_shots and guard(target).get('target') == 'None')
    send('Server', 'occluder', enabled=False)
    place('EX_A', [1400, 4500, 500], shield=100)
    place(target, [1400, 3900, 500], yaw=90)
    wait(lambda: guard(target).get('shots', 0) > 0 and player('Server', 'A').get('health', 100) < 100,
         'guard lands a real hit', 12)
    first_hit = player('Server', 'A').copy()
    g['report']['ai_first_hit'] = first_hit
    # Phase 6A bullets bypass the legacy shield. End the burst before writing
    # the full report so its serialization cannot down the test participant.
    send('Server', 'ammo', target=target, value=0)
    aim_at('A', target)
    send('A', 'ability', kind='smoke')
    check('Guard perceives and shoots a human', lambda: first_hit['health'] < 100 and first_hit['life'] == 0)
    check('Smoke deployment under AI combat succeeds', lambda: len(state('Server').get('smokes', [])) == 1, 5)
    centre = state('Server')['smokes'][0]['position']
    # Stay inside the actual cloud while the guard searches and moves.
    place('EX_A', [centre[0], centre[1]+250, 500], shield=100)
    time.sleep(.6)
    shots = guard(target)['shots']
    check('AI loses target inside smoke', lambda: guard(target).get('state') == 2 and guard(target).get('target') == 'None', 5)
    health = guard(target)['health']
    for _ in range(8):
        send('A', 'pulse', action='Fire')
        time.sleep(.3)
        if guard(target).get('health', health) < health:
            break
    check('Player hits AI through smoke with normal bullets', lambda: guard(target)['health'] < health)
    time.sleep(1)
    check('Smoke suppresses AI fire while allowing human bullets', lambda: guard(target)['shots'] == shots)
    send('A', 'screenshot', name='ai-smoke-combat')
    time.sleep(3)
    check('AI cannot see a human inside the smoke volume', lambda: guard(target)['shots'] == shots)
    check('Smoke expires for every connected client', lambda: all(not state(r)['smokes'] for r in peers), 25)
    shots = guard(target)['shots']
    place('EX_A', [1400, 4500, 500], shield=100)
    place(target, [1400, 3900, 500], yaw=90)
    wait(lambda: guard(target)['shots'] > shots, 'AI reacquires after smoke expires', 12)
    reacquired = guard(target).copy()
    g['report']['ai_reacquisition'] = reacquired
    place('EX_A', [5700, 5000, 180])
    check('AI reacquires after smoke expires', lambda: reacquired['shots'] > shots)
    check('Guard reload consumes its own reserve ammunition', lambda: sum(i['quantity'] for i in guard(target)['inventory']['items'] if i['definition'] == 'Ammo') < 60)
    send('Server', 'ammo', target=target, value=0)
    # The separate death/loot sample approaches from behind. Perception and
    # return fire were already observed above; health damage is still a bullet.
    place(target, [1400, 4200, 500], health=20, yaw=90)
    place('EX_A', [1400, 3900, 500], shield=100)
    aim_at('A', target)
    gun = next(i for i in guard(target)['inventory']['items'] if i['definition'] == 'AK')
    for _ in range(10):
        send('A', 'pulse', action='Fire')
        time.sleep(.4)
        if guard(target).get('dead'):
            break
    check('AI death produces one container with its original weapon instance', lambda: len([
        box for box in state('Server')['loot'] if any(i['id'] == gun['id'] for i in box.get('contents', {}).get('items', []))]) == 1)
    box = next(box for box in state('Server')['loot'] if any(i['id'] == gun['id'] for i in box.get('contents', {}).get('items', [])))
    place('EX_A', [box['position'][0]+100, box['position'][1], 500])
    send('A', 'aim', target='', head=False)
    send('A', 'open_loot', target=box['id'])
    time.sleep(4)
    send('A', 'item', item=gun['id'], from_loot=True, quantity=1, slot=1)
    check('AI rifle enters the real secondary inventory slot', lambda: any(i['id'] == gun['id'] and i['slot'] == 1 for i in player('Server', 'A')['inventory']['items']))
    send('A', 'inventory', open=False)
    send('A', 'screenshot', name='ai-loot')
    exit_at = state('Server')['exit_position']
    place('EX_A', [exit_at[0], exit_at[1], exit_at[2]+90])
    check('AI loot can be extracted into the persistent stash', lambda: player('Server', 'A').get('life') == 3
          and any(i['id'] == gun['id'] for i in state('A')['account']['stash']['items']), 20)
    check('Extracted player only spectates their squad', lambda: state('A').get('spectated') in ['EX_C', 'EX_E'])
    send('A', 'screenshot', name='ai-loot-extracted')
    # Terminal states of other humans do not depend on remaining AI.
    send('C', 'leave')
    check('One squad member can leave while others continue', lambda: state('C').get('map') == '/Game/Maps/Lobby' and not state('C').get('session'))
    send('Server', 'end')
    check('Raid returns and destroys all guard actors', lambda: state('Server').get('map') == '/Game/Maps/Lobby'
          and not state('Server').get('guards'), 40)
    check('AI export remains after returning to the lobby', lambda: any(i['id'] == gun['id'] for i in state('A')['account']['stash']['items']))
    g['report']['ai_export'] = {'profile': state('A')['account']['profile'], 'item': gun['id'], 'definition': 'AK'}
    g['save']()


def lifecycles(g):
    """Three real raids: bleed/timeout, downed exit/disconnect, last-rescuer extraction."""
    send, check, wait, state, player = (g[x] for x in ['send', 'check', 'wait', 'state', 'player'])
    assert len(g['ROLES']) == 6
    positions = {'A': [1400, 4200, 500], 'B': [1400, 3550, 500], 'C': [1550, 4200, 500],
                 'D': [2200, 4000, 500], 'E': [1800, 4600, 500], 'F': [2400, 4600, 500]}

    def place(role, health=100, point=None):
        send('Server', 'fixture', players=[{'name': 'EX_'+role, 'position': point or positions[role], 'health': health, 'shield': 0, 'clear_trauma': True}])
        time.sleep(.3)

    def down(role):
        place(role, 20)
        g['aim']('B', role)
        for _ in range(10):
            send('B', 'pulse', action='Fire')
            time.sleep(.4)
            if player('Server', role).get('life') == 5:
                break
        check(role+' downed for lifecycle boundary', lambda: player('Server', role).get('life') == 5)

    def extract(roles):
        centre = state('Server')['exit_position']
        for i,r in enumerate(roles):
            send(r, 'aim', target='', head=False)
            place(r, point=[centre[0]+(i%3-1)*140, centre[1]+(i//3)*140, centre[2]+90])
        check('Selected survivors extract independently', lambda: all(state(r)['account']['outcome'] == 1 for r in roles), 22)

    def returned():
        check('Lifecycle raid closes and returns to lobby', lambda: state('Server').get('map') == '/Game/Maps/Lobby', 40)

    for r in g['ROLES']:
        place(r)
    down('A')
    deadline = player('Server', 'A')['bleed_at']
    check('Bleedout naturally finalizes downed player', lambda: player('Server', 'A').get('life') == 1, 38)
    check('Bleedout uses original server deadline', lambda: player('Server', 'A').get('bleed_at') == 0 and deadline > 0)
    check('Dead player spectates living squad only', lambda: state('A').get('spectated') in ['EX_C', 'EX_E'], 10)
    down('C')
    send('Server', 'end')
    check('Round timeout settles downed player', lambda: state('C')['account']['outcome'] == 5 and player('Server', 'C').get('life') == 4)
    returned()
    g['begin_round']()
    for r in g['ROLES']:
        place(r)
    down('A')
    leaving_profile = state('A')['account']['profile']
    send('A', 'leave')
    check('Downed voluntary leave completes', lambda: state('A').get('map') == '/Game/Maps/Lobby' and not state('A').get('session'))
    check('Downed leave has one durable loss result', lambda: any(p['profile'] == leaving_profile and p['outcome'] == 3 for p in state('Server')['stored_results']))
    down('C')
    disconnected_profile = state('C')['account']['profile']
    proc = g['processes']['C']
    old = dict(g['report']['processes']['C'])
    g['finishing'].add('C')
    g['subprocess'].run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], capture_output=True, check=True)
    proc.wait(timeout=20)
    old['exit_code'] = proc.returncode
    g['report'].setdefault('disconnects', []).append(old)
    g['report']['processes']['C']['expected_disconnect'] = True
    check('Downed disconnect is settled and releases live membership', lambda: len(state('Server')['players']) == 4, 35)
    check('Downed disconnect settles before bleedout', lambda: any(p['profile'] == disconnected_profile and p['outcome'] == 4 for p in state('Server')['stored_results']))
    extract(['B', 'D', 'E', 'F'])
    returned()
    g['join']('A')
    g['processes'].pop('C')
    (g['OUT']/'C-before-disconnect.log').write_bytes((g['OUT']/'C.log').read_bytes())
    for suffix in ['-state.json', '-command.json']:
        (g['OUT']/('C'+suffix)).unlink(missing_ok=True)
    g['commands'].pop('C', None)
    g['last_states'].pop('C', None)
    g['finishing'].discard('C')
    g['launch']('C')
    g['join']('C')
    check('Disconnected downed profile can return to lobby', lambda: state('C').get('account', {}).get('bound'))
    g['begin_round']()
    for r in g['ROLES']:
        place(r)
    down('A')
    send('C', 'ability', kind='revive', target='EX_A')
    check('Rescuer starts before damage interruption', lambda: player('Server', 'C')['action'] == 1)
    g['aim']('B', 'C')
    for _ in range(5):
        send('B', 'pulse', action='Fire')
        time.sleep(.3)
        if player('Server', 'C')['health'] < 100:
            break
    check('Damage interrupts active rescuer', lambda: player('Server', 'C')['action'] == 0 and player('Server', 'C')['health'] < 100)
    extract(['C', 'E'])
    check('Last living squadmates extracting finalizes the downed player', lambda: player('Server', 'A').get('life') == 1)
    check('No enemy spectator target after last squadmate extracts', lambda: state('A').get('spectating') and not state('A').get('spectated'), 10)
    extract(['B', 'D', 'F'])
    returned()
