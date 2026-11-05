"""Real raid kit and first-person long-gun checks. No inventory/ammunition fixtures.

Use --scenario scar for SpaceCenter or --scenario soviet for the military map.
"""
import hashlib
import json
import math
import runpy
import subprocess
import sys
import time
from pathlib import Path
import scar_scenarios
import soviet_scenarios


def run(g):
    send, check, wait, player, state = (g[k] for k in ('send', 'check', 'wait', 'player', 'state'))
    g['report']['input_sha256']['Scripts/Extraction/run_test_loadout.py'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    g['report']['scope'] = 'Real starting gear; inventory RPC equip, first-person input, firing/reloading and retry; position fixtures only'
    peers = ['Server'] + g['ROLES']
    def items(role='A', peer='Server'):
        return player(peer, role)['inventory']['items']
    for role in g['ROLES']:
        for definition, slot in [('M4A1', 0), ('SCARH', 1), ('Helmet2', 2), ('Vest2', 3), ('Rig_Assault',4), ('Bag_Test', 5), ('BoltSniper', -1), ('PumpShotgun', -1)]:
            check(role + ' starts with ' + definition, lambda r=role, d=definition, s=slot: all(
                any(i['definition'] == d and i['slot'] == s for i in items(r, p)) for p in ['Server', r]))
        check(role+' starts with permanent pockets and two usable grenades',lambda r=role:player('Server',r)['inventory']['personal_pockets']
              and sum(i['quantity'] for i in items(r) if i['definition']=='FragGrenade' and i['slot']!=-3)>=2)
        before = player('Server', role)['inventory']
        send('Server', 'retry_loadout', target='EX_' + role)
        check(role + ' retry preserves all item identities and quantities', lambda r=role, b=before: player('Server', r)['inventory'] == b)
    send('A', 'inventory', open=True)
    check('Test backpack inventory opens', lambda: player('A', 'A')['inventory_open'])
    send('A', 'screenshot', name='test-kit')
    wait(lambda: (g['OUT'] / 'A-test-kit.png').exists(), 'kit screenshot')

    # A known open area; never replace items or ammunition in a fixture.
    position = [-1500, -300, 460] if g['args'].scenario == 'scar' else [-4500, 3000, 300]
    send('Server', 'fixture', players=[dict(name='EX_' + role,
        position=[position[0] - 150 * index, position[1] + 200 * index, position[2]],
        health=100, shield=0, clear_trauma=True) for index, role in enumerate(g['ROLES'])])
    wait(lambda: all(player(p, 'A').get('weapon') for p in peers), 'nearby observer receives shooter')
    send('A', 'look', yaw=0, pitch=0)
    def transfer(item, **fields):
        wait(lambda: player('A', 'A')['inventory'] == player('Server', 'A')['inventory'], 'inventory sync')
        sequence = state('A').get('item_sequence', 0)
        send('A', 'item', item=item, **fields)
        wait(lambda: state('A').get('item_sequence', 0) > sequence, 'item RPC acknowledgement')
        assert not state('A').get('item_message'), state('A').get('item_message')
    def capture(name):
        time.sleep(.6)
        send('A', 'pose', target='EX_A', name=name)
        send('A', 'screenshot', name=name)
        wait(lambda: (g['OUT'] / ('A-' + name + '.png')).exists(), name + ' screenshot')
        (g['OUT'] / (name + '-state.json')).write_text(json.dumps(state('A'), indent=2), encoding='utf-8')
        if name.startswith(('BoltSniper','PumpShotgun')):
            check(name + ' hands stay within 2 cm of rendered grips', lambda: all(
                player('A', 'A').get('fp_actual_' + side + '_grip_distance', 999) < 2 for side in ['left', 'right']), 3)
        if name.endswith('-hip'):
            check(name+' world barrel agrees with view direction',lambda:all(sum(a*b for a,b in zip(
                player(p,'A')['world_weapon_forward'],player(p,'A')['view_forward']))>.6 for p in peers))
    for gun, capacity in [('BoltSniper', 5), ('PumpShotgun', 6)]:
        # The previous gun's walk/sprint check may end against terrain and lower
        # the next gun through the normal obstruction animation.
        send('Server','fixture',players=[dict(name='EX_A',position=position,health=100,shield=0,clear_trauma=True)])
        wait(lambda:math.dist(player('A','A')['position'][:2],position[:2])<5 and not player('A','A')['falling'],'open weapon inspection position')
        send('A','look',yaw=0,pitch=0)
        send('A', 'inventory', open=True)
        wait(lambda: player('A', 'A')['inventory_open'], 'inventory ready')
        # Move the equipped rifle out first; then equip the real backpack weapon.
        held = next(i for i in items() if i['slot'] == 0)
        transfer(held['id'], slot=-1, auto=True)
        check(gun + ' frees primary slot without deleting rifle', lambda: any(i['id'] == held['id'] and i['slot'] < 0 for i in items()))
        weapon = next(i for i in items() if i['definition'] == gun)
        transfer(weapon['id'], slot=0)
        check(gun + ' equipped through inventory RPC', lambda: all(gun in player(p, 'A').get('weapon', '') for p in peers))
        send('A', 'inventory', open=False)
        wait(lambda: player('Server', 'A')['combat_state'] == 0, 'equip finished')
        capture(gun + '-hip')
        # Include the entire world body and its ground shadow, instead of a
        # downward first-person crop which can leave the shadow behind camera.
        actual=player('A','A')['position']
        send('A','world_camera',position=[actual[0]-400,actual[1]-300,actual[2]+400],
             target=[actual[0],actual[1],actual[2]-80],field_of_view=70)
        time.sleep(.6)
        send('A','screenshot',name=gun+'-ground-shadow')
        send('A','camera',target='')
        send('A','look',yaw=0,pitch=0)
        check(gun+' hip crosshair is visible',lambda:player('A','A').get('crosshair_visible'))
        # Keep input held across screenshot encoding and evidence serialization.
        # Release explicitly below; a five-second pulse can expire mid-check.
        send('A', 'pulse', action='Aim', duration=30)
        check(gun + ' ADS completes on owner and authority', lambda: all(player(p, 'A')['aim_alpha'] > .99 for p in ['A', 'Server']))
        capture(gun + '-ads')
        check(gun+' ADS hides screen crosshair',lambda:not player('A','A').get('crosshair_visible'))
        if gun == 'BoltSniper':
            check('Sniper scope clears the opaque gun model', lambda: player('A', 'A')['fp_scope_active']
                  and not player('A', 'A')['fp_weapon_visible'])
            send('A', 'inventory', open=True)
            check('Inventory closes sniper scope', lambda: not player('A', 'A')['fp_scope_active'])
            send('A', 'inventory', open=False)
        send('A', 'pulse', action='Aim', duration=0)
        check(gun + ' exits ADS', lambda: all(player(p, 'A')['aim_alpha'] == 0 for p in ['A', 'Server']))
        if gun == 'BoltSniper':
            check('Leaving scope restores first-person gun', lambda: not player('A', 'A')['fp_scope_active']
                  and player('A', 'A')['fp_weapon_visible'])
        before = player('Server', 'A')['accepted']
        # Native per-frame recording captures the peak even if the command-file
        # polling interval misses a brief render frame.
        send('A','record',enabled=True,name=gun+'-reticle-frames')
        time.sleep(.15)
        send('A', 'pulse', action='Fire', duration=0)
        pulse=player('A','A').get('crosshair_shot_pulse',0)
        time.sleep(.9)
        send('A','record',enabled=False,name=gun+'-reticle-frames')
        g['report'].setdefault('reticle_capture',{})[gun]={'pulse_before_capture':pulse}
        check(gun + ' authoritative shot spends one round', lambda: player('Server', 'A')['accepted'] == before + 1 and player('A', 'A')['ammo'] == capacity - 1)
        send('A', 'pulse', action='Reload', duration=0)
        check(gun + ' reload returns to full magazine', lambda: player('Server', 'A')['ammo'] == capacity and player('A', 'A')['ammo'] == capacity, 20)
        capture(gun + '-reloaded')
        if gun == 'BoltSniper':
            send('A', 'input_key', key='LeftControl', pressed=True)
            send('A', 'input_key', key='LeftControl', pressed=False)
            check('Sniper crouch on owner and authority', lambda: all(player(p, 'A')['crouched'] for p in ['A', 'Server']))
            capture(gun + '-crouch')
            send('A', 'input_key', key='LeftControl', pressed=True)
            send('A', 'input_key', key='LeftControl', pressed=False)
            check('Sniper returns to standing', lambda: not player('A', 'A')['crouched'])
            send('A', 'input_key', key='W', pressed=True)
            capture(gun + '-walk')
            send('A', 'input_key', key='LeftShift', pressed=True)
            check('Sniper sprint on owner and authority', lambda: all(player(p, 'A')['sprinting'] for p in ['A', 'Server']))
            capture(gun + '-sprint')
            send('A', 'input_key', key='LeftShift', pressed=False)
            send('A', 'input_key', key='W', pressed=False)
            check('Sniper exits sprint', lambda: all(not player(p, 'A')['sprinting'] for p in ['A', 'Server']))
            capture(gun + '-sprint-return')
            send('A', 'pulse', action='Aim', duration=30)
            check('Sniper scope reopens before swapping', lambda: player('A', 'A')['fp_scope_active'])
            send('A', 'swap')
            check('Swapping to SCAR clears sniper scope', lambda: 'SCARH' in player('A', 'A').get('weapon', '')
                  and not player('A', 'A')['fp_scope_active'])
            wait(lambda: player('Server', 'A')['combat_state'] == 0, 'SCAR swap finishes')
            send('A', 'pulse', action='Aim', duration=0)
            send('A', 'swap')
            check('Swapping back restores sniper', lambda: all('BoltSniper' in player(p, 'A').get('weapon', '') for p in peers))
            wait(lambda: player('Server', 'A')['combat_state'] == 0, 'sniper swap finishes')
            capture(gun + '-swap-return')
    send('A', 'swap')
    check('SCAR remains usable after long guns', lambda: all('SCARH' in player(p, 'A').get('weapon', '') for p in peers))
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                    str(Path(__file__).with_name('verify_reticle_frames.ps1')),
                    '-RunDirectory', str(g['OUT'])], check=True)
    pixels = json.loads((g['OUT'] / 'ReticlePixels.json').read_text(encoding='utf-8-sig'))
    g['report']['reticle_pixels'] = pixels
    check('Both guns visibly expand and recover in recorded frames', lambda: len(pixels) == 2)


if __name__ == '__main__':
    sys.argv.append('--starting-kit-validation')
    scar_scenarios.run = soviet_scenarios.run = run
    runpy.run_path(str(Path(__file__).with_name('run_network.py')), run_name='__main__')
