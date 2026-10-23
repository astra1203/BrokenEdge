"""Input/RPC acceptance for the extraction mechanics boundary; no damage is injected."""
import time


def run(g):
    send, check, wait, player = (g[k] for k in ('send', 'check', 'wait', 'player'))
    peers = ['Server'] + g['ROLES']
    send('Server', 'fixture', players=[dict(name='EX_' + role, position=[-1500 - 300*i, -300, 460],
                                         health=100, shield=0) for i, role in enumerate(g['ROLES'])])
    check('AKM data version replicated', lambda: len({player(p, 'A')['mechanics_revision'] for p in peers}) == 1
          and player('Server', 'A')['mechanics_revision'] > 0)
    check('AKM source muzzle speed', lambda: player('A', 'A')['mechanics_speed'] == 52500)
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
