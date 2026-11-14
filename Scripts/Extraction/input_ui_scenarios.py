"""Real key bindings and UI cancellation with independent owner/observer peers.

No remapping or focus-loss simulation: those remain in the editor input test.
Only positions are arranged; shots, reload, swap and inventory use production input.
"""
import hashlib
from pathlib import Path
import time


def run(g):
    send, state, player, check, wait = (g[k] for k in ['send', 'state', 'player', 'check', 'wait'])
    peers = ['Server', 'A', 'B']
    assert g['ROLES'] == ['A', 'B']
    g['report']['input_sha256']['Scripts/Extraction/input_ui_scenarios.py'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    g['report']['input_ui_scope'] = 'real default keys; UI cancellation, ammunition and owner/observer replication; position fixtures; no remapping or application focus simulation'

    def key(name, pressed):
        error = send('A', 'input_key', key=name, pressed=pressed)
        assert not error, error

    def tap(name):
        key(name, True)
        time.sleep(.15)
        key(name, False)

    def authoritative(): return player('Server', 'A')
    def inventory_open(): return all(player(p, 'A').get('inventory_open') for p in ['Server', 'A'])
    def idle(): return all(player(p, 'A').get('combat_state') == 0 for p in peers)
    def total_ammo(pawn, ammo_type):
        return sum((i['quantity'] if i['definition'] == ammo_type else 0)
                   + (i.get('magazine', 0) if i.get('ammo_type') == ammo_type else 0)
                   for i in pawn['inventory']['items'])

    send('Server', 'fixture', players=[dict(name='EX_A', position=[-1500, -300, 460], health=100, shield=0),
                                     dict(name='EX_B', position=[-2100, -300, 460], health=100, shield=0)])
    send('A', 'look', yaw=0, pitch=30)
    wait(lambda: not authoritative().get('falling', True) and idle(), 'Input fixture grounded and idle')
    tap('M')
    check('M opens the actual tactical-map widget', lambda: 'ExtractionTacticalMap' in state('A').get('visible_widgets', []))
    tap('Escape')
    check('Escape closes tactical map through input', lambda: 'ExtractionTacticalMap' not in state('A').get('visible_widgets', []))

    tap('Tab')
    check('Tab opens inventory on authority and owner', inventory_open)
    check('Observer receives no private inventory contents', lambda: not player('B', 'A')['inventory']['items'])
    before = authoritative()['accepted']
    key('LeftMouseButton', True)
    time.sleep(.8)
    key('LeftMouseButton', False)
    check('Held fire while inventory is open sends no accepted shots', lambda: authoritative()['accepted'] == before)
    send('A', 'key', key='Escape')
    check('Escape closes the inventory on authority and owner', lambda: all(not player(p, 'A').get('inventory_open') for p in ['Server', 'A']))

    baseline = authoritative()
    ammo_type = baseline['ammo_type']
    initial_ammo = total_ammo(baseline, ammo_type)
    key('LeftMouseButton', True)
    check('Held mouse binding produces real authoritative shots', lambda: authoritative()['accepted'] >= baseline['accepted'] + 2)
    tap('Tab')
    check('Opening inventory during held fire reaches authority', inventory_open)
    time.sleep(.8)  # Drain the recorded delayed packets before checking quiescence.
    stopped = authoritative()
    time.sleep(.8)
    check('Inventory cancels the held-fire loop', lambda: authoritative()['accepted'] == stopped['accepted'])
    key('LeftMouseButton', False)
    send('A', 'key', key='Escape')
    wait(lambda: not authoritative()['inventory_open'], 'Inventory closed after held-fire cancellation')
    time.sleep(.8)
    check('Release in UI and reopening gameplay cannot cause a delayed shot', lambda: authoritative()['accepted'] == stopped['accepted'])
    check('Real shots conserve inventory ammunition', lambda: total_ammo(authoritative(), ammo_type) == initial_ammo - (stopped['accepted'] - baseline['accepted']))
    check('Shot magazine converges on authority owner and observer', lambda: all(player(p, 'A')['ammo'] == stopped['ammo'] for p in peers))

    tap('R')
    check('R binding starts authoritative reload', lambda: authoritative()['combat_state'] == 1)
    tap('Tab')
    check('Inventory cancels reload state on all peers', lambda: inventory_open() and idle())
    time.sleep(2.5)
    check('Cancelled reload cannot transfer ammo after its old timer', lambda: authoritative()['ammo'] == stopped['ammo'] and total_ammo(authoritative(), ammo_type) == total_ammo(stopped, ammo_type))
    send('A', 'screenshot', name='input-reload-cancelled')
    send('A', 'key', key='Escape')
    check('Owner and authority close inventory before the next input', lambda: idle()
          and all(not player(p, 'A')['inventory_open'] for p in ['Server', 'A'])
          and 'WBP_LootingMainView_Pc_C' not in state('A').get('visible_widgets', []))
    # IMC removal happens in the controller tick after InventoryOpen replication.
    # Wait for that mapping rebuild; a key pressed in the old UI context is ignored until released.
    time.sleep(.35)
    tap('R')
    check('A fresh R press after UI removal starts reload', lambda: authoritative()['combat_state'] == 1, 15)
    check('Valid reload after cancellation finishes on every peer', lambda: idle() and all(player(p, 'A')['ammo'] > stopped['ammo'] for p in peers), 15)
    check('Successful reload preserves total inventory ammunition', lambda: total_ammo(authoritative(), ammo_type) == total_ammo(stopped, ammo_type))
    # The original interior has supply containers on both sides. Use the authored
    # exit's known ground for this separate no-interaction-target swap fixture.
    exit_point = next(e for e in state('Server')['exits'] if e['id'] == 'OfficialExitTestrange')['bounds_center']
    positions = [dict(name='EX_' + role, position=[exit_point[0] + offset, exit_point[1], exit_point[2]], health=100, shield=0)
                 for role, offset in [('A', -130), ('B', 130)]]
    g['report']['input_ui_swap_fixture'] = positions
    send('Server', 'fixture', players=positions)
    send('A', 'look', yaw=0, pitch=0)
    wait(lambda: not authoritative().get('falling', True), 'Swap fixture grounded')
    check('Swap fixture has no local loot focus', lambda: player('A', 'A').get('focus') == 'None')
    original = authoritative()['weapon_item']
    tap('E')
    check('E swaps the held weapon and converges on all peers', lambda: idle() and authoritative()['weapon_item'] != original
          and all(player(p, 'A')['weapon_item'] == authoritative()['weapon_item'] for p in peers), 15)
    tap('E')
    check('Second E returns to the original weapon instance', lambda: idle() and all(player(p, 'A')['weapon_item'] == original for p in peers), 15)
    check('Input sequence leaves observer inventory private', lambda: not player('B', 'A')['inventory']['items'])
    send('A', 'screenshot', name='input-complete')
