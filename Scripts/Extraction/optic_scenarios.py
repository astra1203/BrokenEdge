"""Focused optic captures, using the existing two-client equipment acceptance."""
import json
import time
from verify_optic_capture import measure


def run(g, capture, operation, gun, item, parts):
    send, check, wait, player = (g[k] for k in ('send', 'check', 'wait', 'player'))
    measurements = []
    send('B', 'aim', target='', head=False)
    for posture in ['standing', 'crouched']:
        if posture == 'crouched':
            send('B', 'pulse', action='Crouch')
            time.sleep(.5)
        for pitch in [0, 35, -35]:
            send('B', 'look', yaw=0, pitch=pitch)
            send('B', 'pulse', action='Aim', duration=5)
            time.sleep(1.2)
            name = 'optic-' + posture + '-' + str(pitch)
            capture('B', name)
            result = measure(g['OUT'] / ('B-' + name + '.png'))
            measurements.append(result)
            (g['OUT'] / 'OpticPixels.json').write_text(json.dumps(measurements, indent=2), encoding='utf-8')
            check('Optic reticle within two pixels: ' + name, lambda r=result: r['passed'])
            send('B', 'pulse', action='Aim', duration=.04)
            time.sleep(.2)
    send('B', 'pulse', action='Crouch')
    time.sleep(.4)
    send('B', 'look', yaw=0, pitch=0)
    assert not operation('B', gun('B')['id'], kind='detach', attachment_slot='optic')
    check('Detaching optic restores its exact identity', lambda: item('B', 'RedDot')['id'] == parts['RedDot'])
    wait(lambda: all('RedDot' not in player(p, 'B')['weapon_parts'] for p in ['Server', 'A', 'B']), 'optic removed on all peers')
    send('B', 'pulse', action='Aim', duration=3)
    time.sleep(1)
    capture('B', 'optic-detached-ironsights')
    send('B', 'pulse', action='Aim', duration=.04)
    time.sleep(.3)
    assert not operation('B', parts['RedDot'], kind='attach', weapon=gun('B')['id'])
    send('B', 'pulse', action='Aim', duration=3)
    time.sleep(1)
    capture('B', 'optic-reattached')
    result = measure(g['OUT'] / 'B-optic-reattached.png')
    measurements.append(result)
    (g['OUT'] / 'OpticPixels.json').write_text(json.dumps(measurements, indent=2), encoding='utf-8')
    check('Reattached optic returns to viewport center', lambda: result['passed'])
    send('B', 'pulse', action='Aim', duration=.04)
    time.sleep(.3)
