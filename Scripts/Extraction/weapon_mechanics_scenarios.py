"""Input/RPC acceptance for the extraction mechanics boundary; no damage is injected."""
import time


def sprint_fire(g, weapon):
    send, check, player = (g[k] for k in ('send', 'check', 'player'))
    send('A', 'aim', target='', head=False)
    send('A', 'look', yaw=0, pitch=0)
    send('Server', 'fixture', players=[dict(name='EX_A', position=[-1500, -300, 460],
                                         health=100, shield=0, clear_trauma=True)])
    send('A', 'input_key', key='W', pressed=True)
    send('A', 'input_key', key='LeftShift', pressed=True)
    check(weapon + ' owner and server sprint', lambda: player('A', 'A')['sprinting']
          and player('Server', 'A')['sprinting'])
    before = player('Server', 'A')['accepted']
    send('A', 'input_key', key='LeftMouseButton', pressed=True)
    check(weapon + ' held Shift permits sprint-to-fire', lambda: player('Server', 'A')['accepted'] > before
          and not player('Server', 'A')['sprinting'] and not player('A', 'A')['sprinting'])
    send('A', 'input_key', key='LeftMouseButton', pressed=False)
    check(weapon + ' release restores sprint on owner and server', lambda: player('A', 'A')['sprinting']
          and player('Server', 'A')['sprinting'])
    send('A', 'input_key', key='LeftShift', pressed=False)
    send('A', 'input_key', key='W', pressed=False)
    check(weapon + ' stop movement exits sprint', lambda: not player('Server', 'A')['sprinting'])
    send('A', 'pulse', action='Reload', duration=0)
    check(weapon + ' reload restores test magazine', lambda: player('A', 'A')['ammo'] ==
          (30 if weapon == 'AK' else 20), 15)


def run(g):
    send, check, wait, player = (g[k] for k in ('send', 'check', 'wait', 'player'))
    peers = ['Server'] + g['ROLES']
    send('Server', 'fixture', players=[dict(name='EX_' + role, position=[-1500 - 300*i, -300, 460],
                                         health=100, shield=0) for i, role in enumerate(g['ROLES'])])
    check('AKM data version replicated', lambda: len({player(p, 'A')['mechanics_revision'] for p in peers}) == 1
          and player('Server', 'A')['mechanics_revision'] > 0)
    check('AKM source muzzle speed', lambda: player('A', 'A')['mechanics_speed'] == 52500)
    sprint_fire(g, 'AK')
    send('A', 'input_key', key='RightMouseButton', pressed=True)
    check('Owner and authority complete ADS', lambda: player('A', 'A')['aim_alpha'] > .99
          and player('Server', 'A')['aim_alpha'] > .99)
    send('A', 'input_key', key='LeftAlt', pressed=True)
    check('Hold breath consumes authority budget', lambda: player('Server', 'A')['breath'] < 98)
    send('A', 'input_key', key='LeftAlt', pressed=False)
    send('A', 'input_key', key='RightMouseButton', pressed=False)
    check('ADS exits and breath recovers', lambda: player('A', 'A')['aim_alpha'] == 0
          and player('Server', 'A')['breath'] >= 99)
    before = player('Server', 'A')
    send('A', 'invalid', kind='version')
    check('Reject stale mechanics version', lambda: player('Server', 'A')['rejected'] > before['rejected'])
    check('Rejected version spends no ammunition', lambda: player('Server', 'A')['ammo'] == before['ammo'])
    send('A', 'input_key', key='RightMouseButton', pressed=True)
    check('ADS before swap', lambda: player('Server', 'A')['aim_alpha'] > .99)
    send('A', 'input_key', key='LeftAlt', pressed=True)
    check('Breath held before swap', lambda: player('Server', 'A')['breath'] < 98)
    send('A', 'input_key', key='Two', pressed=True)
    send('A', 'input_key', key='Two', pressed=False)
    wait(lambda: player('Server', 'A')['combat_state'] == 0 and 'SCARH' in player('Server', 'A')['weapon'], 'SCAR swap')
    check('SCAR source muzzle speed on peers', lambda: all(player(p, 'A')['mechanics_speed'] == 63000 for p in peers))
    check('Swap clears held breath without another key press', lambda: player('Server', 'A')['breath'] >= 99)
    send('A', 'input_key', key='LeftAlt', pressed=False)
    send('A', 'input_key', key='RightMouseButton', pressed=False)
    sprint_fire(g, 'SCARH')
    send('Server', 'fixture', players=[dict(name='EX_A', position=[-1500, -300, 460],
                                         health=100, shield=0, clear_trauma=True)])
    send('A', 'aim', target='EX_B', head=False)
    send('A', 'input_key', key='RightMouseButton', pressed=True)
    time.sleep(.7)
    before = player('Server', 'A')
    victim = player('Server', 'B')
    send('A', 'pulse', action='Fire', duration=0)
    check('One authoritative physical shot', lambda: player('Server', 'A')['accepted'] == before['accepted'] + 1)
    check('Owner ammo converges', lambda: player('Server', 'A')['ammo'] == before['ammo'] - 1
          and player('A', 'A')['ammo'] == before['ammo'] - 1)
    check('Physical bullet hits exactly once', lambda: player('Server', 'B')['bullet_sequence'] == victim['bullet_sequence'] + 1)
    time.sleep(.3)
    check('No duplicate hitscan damage', lambda: player('Server', 'B')['bullet_sequence'] == victim['bullet_sequence'] + 1
          and player('Server', 'B')['health'] < victim['health'])
    check('Recoil sequence reconciles', lambda: player('A', 'A')['burst_index'] == player('Server', 'A')['burst_index'])
    send('A', 'screenshot', name='mechanics-scar-ads')
    if 'B' in g['rendered_roles']:
        send('A', 'aim', target='', head=False)
        send('A', 'look', yaw=90, pitch=0)
        send('B', 'aim', target='EX_A', head=False)
        time.sleep(.5)
        send('B', 'screenshot', name='mechanics-observer')
        wait(lambda: (g['OUT'] / 'B-mechanics-observer.png').exists(), 'Rendered observer screenshot')
        send('B', 'aim', target='', head=False)
    send('A', 'input_key', key='RightMouseButton', pressed=False)

    send('A', 'inventory', open=True)
    check('Weapon inspection inventory opens', lambda: player('A', 'A')['inventory_open'])
    check('Inventory clears aiming pose', lambda: player('A', 'A')['aim_alpha'] == 0)
    send('A', 'mouse', x=155, y=180, action='down', button='right')
    send('A', 'mouse', x=155, y=180, action='up', button='right')
    send('A', 'screenshot', name='mechanics-stats-panel')
    wait(lambda: (g['OUT'] / 'A-mechanics-stats-panel.png').exists(), 'Weapon details screenshot')
    send('A', 'mouse', x=200, y=355, action='down', button='left')
    send('A', 'mouse', x=200, y=355, action='up', button='left')
    time.sleep(.5)
    send('A', 'screenshot', name='mechanics-gunsmith')
    wait(lambda: (g['OUT'] / 'A-mechanics-gunsmith.png').exists(), 'Gunsmith panel screenshot')
    check('Gunsmith production entry opened', lambda: 'EXTRACTION_GUNSMITH_OPEN' in
          (g['OUT'] / 'A.log').read_text(encoding='utf-8', errors='replace'))
    for x, y in [(800, 125), (380, 620)]:
        send('A', 'mouse', x=x, y=y, action='down', button='left')
        send('A', 'mouse', x=x, y=y, action='up', button='left')
    check('Gunsmith preview apply unloads through authority', lambda: player('Server', 'A')['ammo'] == 0
          and player('A', 'A')['ammo'] == 0)
    send('A', 'screenshot', name='mechanics-gunsmith-applied')
    wait(lambda: (g['OUT'] / 'A-mechanics-gunsmith-applied.png').exists(), 'Applied gunsmith screenshot')
    for action in ['down', 'up']:
        send('A', 'mouse', x=900, y=620, action=action, button='left')
    send('A', 'inventory', open=False)
    send('A', 'aim', target='', head=False)
    send('A', 'pulse', action='Reload', duration=0)
    check('Applied unload can reload in live combat', lambda: player('A', 'A')['ammo'] == 20, 15)

    # Every joined owner submits a real input shot; reset position/health only, never inject damage.
    for i, role in enumerate(g['ROLES']):
        target = g['ROLES'][i + 1 if i + 1 < len(g['ROLES']) else i - 1]
        # Reuse the validated unobstructed comparison lane for every owner.
        positions = {r: [-2100-160*j, -300, 460] for j, r in enumerate(g['ROLES'])}
        positions.update({role: [-1500, -300, 460], target: [-1800, -300, 460]})
        send('Server', 'fixture', players=[dict(name='EX_' + r, position=positions[r],
                                             health=100, shield=0, clear_trauma=True)
                                          for j, r in enumerate(g['ROLES'])])
        send(role, 'aim', target='EX_' + target, head=False)
        send(role, 'input_key', key='RightMouseButton', pressed=True)
        wait(lambda r=role: player('Server', r)['aim_alpha'] > .99 and not player('Server', r)['falling'], 'Owner ready to fire')
        before = player('Server', role)
        hit = player('Server', target)['bullet_sequence']
        send(role, 'pulse', action='Fire', duration=0)
        check(role + ' input shot accepted once', lambda r=role, b=before: player('Server', r)['accepted'] == b['accepted'] + 1)
        check(role + ' owner ammo and configuration agree', lambda r=role, b=before:
              player(r, r)['ammo'] == player('Server', r)['ammo'] == b['ammo'] - 1
              and all(player(p, r)['mechanics_revision'] == player('Server', r)['mechanics_revision'] for p in peers))
        check(role + ' physical damage sequence advances once', lambda t=target, h=hit: player('Server', t)['bullet_sequence'] == h + 1)
        send(role, 'input_key', key='RightMouseButton', pressed=False)
        send(role, 'aim', target='', head=False)

    expected = {r: [i for i in player('Server', r)['inventory']['items'] if i['definition'] in ['AK', 'SCARH']]
                for r in g['ROLES']}
    center = g['state']('Server')['exit_position']
    for role in g['ROLES']:
        send('Server', 'fixture', players=[dict(name='EX_' + role, position=[center[0], center[1], center[2]+90],
                                             health=100, shield=0, clear_trauma=True)])
        check(role + ' weapon loadout exits through normal settlement', lambda r=role:
              g['state'](r).get('account', {}).get('outcome') == 1, 25)
        stored = g['state'](role)['account']['stash']['items'] + g['state'](role)['account']['overflow']['items']
        check(role + ' actual magazines and assemblies persist', lambda r=role, s=stored: all(
              any(j['id'] == i['id'] and j['magazine'] == i['magazine'] and j['attachments'] == i['attachments'] for j in s)
              for i in expected[r]))
    g['report']['scope'] = 'Real input shots for every owner, authoritative damage/ammo/config convergence and normal settlement; position/health fixtures only.'
