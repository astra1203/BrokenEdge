"""Focused regression at the packaged route's blocked entrance, with placement fixtures.

Arguments are passed to run_network.py, e.g. --scenario soviet --players 2 --guards 0.
The route acceptance remains separate and uses no position fixtures.
"""
import json
import hashlib
import runpy
import time
from pathlib import Path
import soviet_scenarios


def run(g):
    g['report']['input_sha256']['Scripts/Extraction/run_soviet_stairs.py'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    g['report']['scope'] = 'Three entrance approaches using explicit start-position fixtures and normal walking input'
    send, check, state, player = (g[k] for k in ('send', 'check', 'state', 'player'))
    trials = [
        ('side entry', [-4750, 2320.35, 200], [-4976.91, 2320.35, 133.76]),
        ('front entry', [-4977, 2500, 200], [-4977, 2300, 140]),
        ('opposite side entry', [-5200, 2320.35, 200], [-4976.91, 2320.35, 133.76]),
    ]
    records = []
    for label, start, end in trials:
        send('Server', 'fixture', players=[dict(name='EX_A', position=start, health=100,
                                               shield=0, clear_trauma=True)])
        time.sleep(1)
        send('A', 'walk', points=[end])
        def done():
            s = state('A')
            assert not s.get('error'), s.get('error')
            return s.get('walk_count') == 1 and s.get('walk_index') == 1
        try:
            check('Entrance stair ' + label, done, 50)
        finally:
            records.append(dict(label=label, start=start, target=end,
                                actual=player('Server', 'A').get('position'),
                                error=state('A').get('error'), probe=state('A').get('walk_probe')))
            (g['OUT'] / 'Stairs.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
        send('A', 'screenshot', name='stair-' + label.replace(' ', '-'))


if __name__ == '__main__':
    soviet_scenarios.run = run
    runpy.run_path(str(Path(__file__).with_name('run_network.py')), run_name='__main__')
