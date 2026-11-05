"""Every authored door: blocked walking while closed, F/open, cross, close, both directions.

Start positions are explicit fixtures; crossings use normal character walking.
"""
import hashlib
import json
import math
import runpy
import time
from pathlib import Path
import soviet_scenarios


def run(g):
    send, check, wait, state, player = (g[k] for k in ('send', 'check', 'wait', 'state', 'player'))
    g['report']['input_sha256']['Scripts/Extraction/run_soviet_doors.py'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    g['report']['scope'] = 'All 17 doors, both directions: real closed blocking and open walking; start-position fixtures; server and owner positions'
    peers = ['Server'] + g['ROLES']
    check('17 doors on all peers', lambda: all(len(state(p).get('ordinary_doors', [])) == 17 for p in peers))
    doors = sorted(state('Server')['ordinary_doors'], key=lambda d: (d['position'][2], d['name']))
    records = []
    for door in doors:
        center = door['position']
        delta = [door['approach'][i] - center[i] for i in range(2)]
        length = math.hypot(*delta)
        normal = [d / length for d in delta]
        def leaf(peer):
            return next(d for d in state(peer)['ordinary_doors'] if d['name'] == door['name'])
        def signed(peer):
            p = player(peer, 'A')['position']
            return sum((p[i] - center[i]) * normal[i] for i in range(2))
        for side in [1, -1]:
            label = door['name'] + (' front' if side == 1 else ' back')
            start = [center[i] + side * normal[i] * 180 for i in range(2)] + [door['approach'][2]]
            end = [center[i] - side * normal[i] * 180 for i in range(2)] + [door['approach'][2]]
            send('Server', 'fixture', players=[dict(name='EX_A', position=start, health=100, shield=0, clear_trauma=True)])
            time.sleep(.45)
            send('A', 'walk', points=[end])
            time.sleep(1.5)
            blocked = {p: player(p, 'A')['position'] for p in ['Server', 'A']}
            send('A', 'walk', points=[])
            check(label + ' closed leaf blocks actual walking on server and owner', lambda: all(
                15 < signed(p) * side < 130 for p in ['Server', 'A']), 3)
            send('Server', 'fixture', players=[dict(name='EX_A', position=start, health=100, shield=0, clear_trauma=True)])
            send('A', 'look', yaw=door['yaw'] + (180 if side < 0 else 0), pitch=0)
            time.sleep(.35)
            if door == doors[0] and side == 1:
                send('A', 'input_key', key='F', pressed=True)
                send('A', 'input_key', key='F', pressed=False)
            else:
                send('A', 'ordinary_door', name=door['name'])
            check(label + ' opening replicated', lambda: all(leaf(p)['open'] and abs(leaf(p)['angle']) > 90 for p in peers), 5)
            send('A', 'walk', points=[end])
            check(label + ' open doorway permits normal walking on server and owner', lambda: all(signed(p) * side < -125 for p in ['Server', 'A']), 8)
            send('A', 'walk', points=[])
            records.append(dict(door=door['name'], side=side, closed_positions=blocked,
                                open_positions={p: player(p, 'A')['position'] for p in ['Server', 'A']}))
            (g['OUT'] / 'DoorCrossings.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
            if door == doors[0]:
                send('A', 'screenshot', name='door-crossed-' + str(side))
            send('A', 'ordinary_door', name=door['name'])
            check(label + ' closing replicated', lambda: all(not leaf(p)['open'] and abs(leaf(p)['angle']) < 1 for p in peers), 5)


if __name__ == '__main__':
    soviet_scenarios.run = run
    runpy.run_path(str(Path(__file__).with_name('run_network.py')), run_name='__main__')
