"""Live easy-guard regression: timing, real bullets, smoke, reload and looted weapon."""
import time
import hashlib
from pathlib import Path


def run(g):
    g['report']['input_sha256']['Scripts/Extraction/guard_tuning_scenarios.py'] = hashlib.sha256(
        Path(__file__).read_bytes()).hexdigest()
    send, check, wait, state, player = (g[k] for k in ['send', 'check', 'wait', 'state', 'player'])
    peers = ['Server', 'A', 'B']

    def guard():
        return next(v for v in state('Server')['guards'] if v['name'] == 'GUARD_0')

    def place(name, position, health=100, yaw=90):
        send('Server', 'fixture', players=[dict(name=name, position=position, health=health,
                                              shield=0, clear_trauma=True, yaw=yaw)])

    def safe_players():
        place('EX_A', [5700, 5000, 180])
        place('EX_B', [6000, 5000, 180])

    def full_damage():
        g['fixture']()
        g['aim']('A', 'B')
        for _ in range(12):
            before = player('Server', 'B').copy()
            shots = player('Server', 'A')['accepted']
            send('A', 'pulse', action='Fire', duration=0)
            wait(lambda: player('Server', 'A')['accepted'] == shots + 1, 'one player bullet')
            time.sleep(.35)
            after = player('Server', 'B')
            if after['bullet_sequence'] > before['bullet_sequence'] and after['hit_loss'] == 20:
                check('Player body bullet deals 20 HP on all peers', lambda: all(
                    abs(player(p, 'B')['health'] - after['health']) <= 1 for p in peers))
                return
            place('EX_B', before['position'], health=80)
            time.sleep(.3)
        raise AssertionError('No normal 20 HP body hit after 12 scatter samples')

    check('Four 100 HP guards replicate', lambda: all(len(state(p).get('guards', [])) == 4
          and all(v['health'] == 100 for v in state(p)['guards']) for p in peers))
    check('All four guards reach patrol points', lambda: all(v.get('arrivals', 0) >= 2
          for v in state('Server')['guards']), 100)
    for i in range(1, 4):
        place('GUARD_' + str(i), [-4300 + i * 200, -6300, 180])

    # Sample server world time via the round countdown, not wall-clock loading time.
    place('GUARD_0', [1400, 3900, 500])
    wait(lambda: guard().get('target') == 'None', 'guard has no target')
    before = guard()['shots']
    place('EX_A', [1400, 4500, 500])
    wait(lambda: guard().get('target') != 'None', 'guard acquires player')
    acquired = state('Server')['remaining']
    samples, hits = [], []
    previous = before
    sequence = player('Server', 'A').get('bullet_sequence', 0)
    deadline = time.monotonic() + 15
    while len(samples) < 5 and time.monotonic() < deadline:
        snap = state('Server')
        current = next(v for v in snap['guards'] if v['name'] == 'GUARD_0')
        victim = next(v for v in snap['players'] if v['name'] == 'EX_A')
        if current['shots'] != previous:
            assert current['shots'] == previous + 1, 'Snapshot missed a shot'
            samples.append(snap['remaining'])
            previous = current['shots']
        if victim.get('bullet_sequence', 0) != sequence:
            hits.append(dict(loss=victim['hit_loss'], bone=victim['hit_bone']))
            sequence = victim['bullet_sequence']
        time.sleep(.025)
    safe_players()
    reaction = acquired - samples[0] if samples else 0
    intervals = [a - b for a, b in zip(samples, samples[1:])]
    g['report']['guard_tuning'] = dict(reaction_seconds=reaction, shot_intervals=intervals, hits=hits)
    # Driver snapshots are emitted at 0.1-second intervals; tolerate their sampling error.
    check('Guard reacts in approximately two seconds', lambda: 1.7 <= reaction <= 2.4)
    check('Five shots respect the 0.8 second cadence', lambda: len(samples) == 5
          and all(.6 <= v <= 1.2 for v in intervals) and samples[0] - samples[-1] >= 3.0)
    check('Guard body bullets deal 6 HP, exactly 30 percent of 20', lambda: len(hits) >= 2
          and all(abs(v['loss'] - (12 if v['bone'].lower() == 'head' else 6)) < .01 for v in hits))

    # A normal player keeps full damage.
    place('GUARD_0', [-4900, -6300, 180])
    full_damage()
    safe_players()

    # Occlusion, smoke, and reload retain the existing production behavior.
    send('Server', 'occluder', enabled=True)
    place('EX_A', [1800, 4200, 500])
    place('GUARD_0', [1400, 4200, 500], yaw=0)
    wait(lambda: guard().get('target') == 'None', 'wall blocks acquisition')
    before = guard()['shots']
    for _ in range(12):
        place('GUARD_0', [1400, 4200, 500], yaw=0)
        time.sleep(.15)
    check('Opaque wall prevents shooting', lambda: guard()['shots'] == before)
    send('Server', 'occluder', enabled=False)
    place('EX_A', [1400, 4500, 500])
    place('GUARD_0', [1400, 3900, 500])
    send('A', 'aim', target='GUARD_0', head=False)
    send('A', 'ability', kind='smoke')
    wait(lambda: bool(state('Server').get('smokes')), 'smoke deploys', 5)
    centre = state('Server')['smokes'][0]['position']
    place('EX_A', [centre[0], centre[1] + 250, 500])
    check('Smoke breaks guard targeting', lambda: guard().get('target') == 'None', 5)
    before = guard()['shots']
    time.sleep(3)
    check('Guard does not shoot through smoke', lambda: guard()['shots'] == before)
    send('Server', 'ammo', target='GUARD_0', value=0)
    wait(lambda: not state('Server').get('smokes'), 'smoke expires', 25)
    place('EX_A', [1400, 4500, 500])
    place('GUARD_0', [1400, 3900, 500])
    check('Guard reacquires and fires after reloading', lambda: guard()['shots'] > before, 12)
    check('Reload consumes guard reserve ammo', lambda: sum(v['quantity'] for v in
          guard()['inventory']['items'] if v['definition'] == 'Ammo') < 60)
    safe_players()

    # Kill, loot, and use the very same weapon instance against an unarmored player.
    send('Server', 'ammo', target='GUARD_0', value=0)
    place('GUARD_0', [1400, 4200, 500], health=20)
    place('EX_A', [1400, 3900, 500])
    send('A', 'aim', target='GUARD_0', head=False)
    gun = next(v for v in guard()['inventory']['items'] if v['definition'] == 'AK')['id']
    for _ in range(10):
        send('A', 'pulse', action='Fire', duration=0)
        time.sleep(.4)
        if guard()['dead']:
            break
    check('Guard can be killed by player fire', lambda: guard()['dead'])
    boxes = [v for v in state('Server')['loot'] if any(i['id'] == gun for i in v['contents']['items'])]
    check('Guard drops exactly one original weapon', lambda: len(boxes) == 1)
    box = boxes[0]
    place('EX_A', [box['position'][0] + 100, box['position'][1], 500])
    send('A', 'aim', target='', head=False)
    send('A', 'open_loot', target=box['id'])
    wait(lambda: any(i['id'] == gun for i in player('A', 'A').get('loot_view', {}).get('items', [])),
         'search reveals guard rifle', 15)
    send('A', 'item', item=gun, from_loot=True, quantity=1, slot=1)
    check('Original rifle is in player secondary slot', lambda: any(i['id'] == gun and i['slot'] == 1
          for i in player('Server', 'A')['inventory']['items']))
    send('A', 'inventory', open=False)
    send('A', 'swap')
    check('Player equips looted rifle', lambda: player('Server', 'A').get('weapon_item') == gun)
    wait(lambda: player('Server', 'A')['combat_state'] == 0, 'weapon swap finishes')
    send('A', 'pulse', action='Reload', duration=0)
    check('Looted rifle reloads normally', lambda: player('Server', 'A').get('ammo', 0) > 0)
    full_damage()
    check('Looted guard rifle retains full player damage', lambda: player('Server', 'B')['hit_loss'] == 20)
    send('A', 'screenshot', name='easy-guards-looted-rifle')
    exit_at = state('Server')['exit_position']
    place('EX_A', [exit_at[0], exit_at[1], exit_at[2] + 90])
    check('Looted rifle survives extraction', lambda: player('Server', 'A').get('life') == 3
          and any(i['id'] == gun for i in state('A')['account']['stash']['items']), 20)
    send('Server', 'end')
    check('Raid returns to lobby', lambda: all(state(p).get('map') == '/Game/Maps/Lobby' for p in peers), 40)
