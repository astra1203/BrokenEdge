"""Stage-4 production RPC/Slate scenarios; fixtures only arrange scene or full stash."""
import shutil
import subprocess
import time


def run(g):
    state, player, send, check, wait = (g[k] for k in ['state', 'player', 'send', 'check', 'wait'])
    scenario = g['args'].scenario

    def account(role):
        return state(role).get('account', {})

    def count(role, definition, side='stash'):
        return sum(i['quantity'] for i in account(role).get(side, {}).get('items', []) if i['definition'] == definition)

    def position(role, xyz, health=80):
        send('Server', 'fixture', players=[{'name': 'EX_' + role, 'position': xyz, 'health': health, 'shield': 0}])
        time.sleep(.4)

    def item_action(role, ident, **fields):
        wait(lambda: player(role, role)['inventory'] == player('Server', role)['inventory'], role + ' inventory synchronized')
        previous = state(role).get('item_sequence', 0)
        send(role, 'item', item=ident, **fields)
        wait(lambda: state(role).get('item_sequence', 0) > previous, role + ' item result')
        assert not state(role).get('item_message'), state(role).get('item_message')

    def mouse(role, x, y, action, button='left'):
        send(role, 'mouse', x=x, y=y, action=action, button=button)

    def click(role, x, y, button='left'):
        mouse(role, x, y, 'down', button)
        mouse(role, x, y, 'up', button)

    def leave_zone(role):
        position(role, [1400, 3300, 500])
        check(role + ' extraction timer reset outside zone', lambda: player('Server', role).get('extract_at') == 0)

    def enter_zone(role):
        position(role, [1400, 2900, 500])
        check(role + ' server starts individual extraction', lambda: player('Server', role).get('extract_at', 0) > 0)

    def extracted(role):
        check(role + ' durably extracted', lambda: player('Server', role).get('life') == 3 and account(role).get('outcome') == 1, 20)
        check(role + ' has no live inventory or pawn', lambda: not player('Server', role)['inventory']['items'] and not state(role).get('has_pawn'))

    if scenario == 'settlement':
        profile = account('A')['profile']
        before = {i['id'] for i in player('Server', 'A')['inventory']['items']}
        send('Server', 'stash_fill', profile=profile)
        position('B', [1400, 3450, 500])
        enter_zone('A')
        time.sleep(2)
        leave_zone('A')
        time.sleep(1)
        check('Leaving cannot finalize extraction', lambda: account('A')['outcome'] == 0)
        enter_zone('A')
        at = player('Server', 'A')['extract_at']
        g['aim']('B', 'A')
        send('B', 'screenshot', name='extraction-approach')
        g['shot']('B', 'A', 20)
        check('Damage does not reset extraction deadline', lambda: player('Server', 'A')['extract_at'] == at)
        send('A', 'screenshot', name='extraction-countdown')
        extracted('A')
        send('A', 'screenshot', name='extraction-result')
        check('Full stash preserves successful loot in overflow', lambda: len(account('A')['overflow']['items']) == 5)
        check('Successful items never become a death container', lambda: not any(before.intersection(i['id'] for i in box['contents']['items']) for box in state('Server')['loot']))
        check('Other player keeps playing after individual success', lambda: state('Server')['phase'] == 1 and player('Server', 'B')['life'] == 0)
        send('B', 'aim', target='', head=False)
        enter_zone('B'); extracted('B')
        check('All terminal players return to ready lobby', lambda: all(state(r).get('map') == '/Game/Maps/Lobby' for r in g['processes']), 35)
        check('Returned accounts retain settlement and readiness resets', lambda: account('A')['outcome'] == 1 and account('B')['outcome'] == 1 and all(not p.get('ready') for p in state('Server')['players']))
        send('A', 'stash_ui'); time.sleep(.6)
        send('A', 'screenshot', name='stash-overflow')
        old = account('A')['revision']
        click('A', 700, 465)
        check('Full stash rejects claim without losing overflow', lambda: bool(account('A').get('message')) and account('A')['revision'] == old and len(account('A')['overflow']['items']) == 5)
        ammo = next(i for i in account('A')['stash']['items'] if i['definition'] == 'Ammo')
        x, y = 105 + ammo['x'] * 38 + 10, 165 + ammo['y'] * 38 + 10
        click('A', x, y, 'right'); time.sleep(.2)
        click('A', min(x, 1020) + 12, min(y, 460) + 15)
        check('Actual context menu transfers to loadout', lambda: count('A', 'Ammo', 'loadout') == 60)
        claim = next(n for n, i in enumerate(account('A')['overflow']['items']) if i['definition'] == 'Ammo')
        click('A', 700, 465 + claim * 29)
        check('Claim consumes overflow only after fitting', lambda: len(account('A')['overflow']['items']) == 4)
        medicine = next(i for i in account('A')['stash']['items'] if i['definition'] == 'Medkit')
        mouse('A', 105 + medicine['x'] * 38 + 10, 165 + medicine['y'] * 38 + 10, 'down')
        mouse('A', 660 + 2 * 38 + 10, 245 + 38 + 10, 'move')
        send('A', 'key', key='R')
        send('A', 'screenshot', name='stash-drag-rotation')
        mouse('A', 660 + 2 * 38 + 10, 245 + 38 + 10, 'up')
        check('Stash drag rotates into loadout grid', lambda: any(i['id'] == medicine['id'] and i['rotated'] for i in account('A')['loadout']['items']))
        ammo = next(i for i in account('A')['loadout']['items'] if i['definition'] == 'Ammo')
        x, y = 660 + ammo['x'] * 38 + 10, 245 + ammo['y'] * 38 + 10
        click('A', x, y, 'right'); time.sleep(.2)
        click('A', min(x, 1020) + 12, min(y, 460) + 4 * 31 + 12)
        send('A', 'key', key='Up')
        send('A', 'screenshot', name='stash-split-quantity')
        send('A', 'key', key='Enter')
        mouse('A', 660 + 5 * 38 + 10, 245 + 2 * 38 + 10, 'move')
        mouse('A', 660 + 5 * 38 + 10, 245 + 2 * 38 + 10, 'up')
        check('UI split preserves stored reserve and creates two stacks', lambda: count('A', 'Ammo', 'loadout') == 60 and sorted(i['quantity'] for i in account('A')['loadout']['items'] if i['definition'] == 'Ammo') == [29, 31])
        send('A', 'key', key='Escape')
        send('A', 'ready')
        check('Preparing locks account editing', lambda: account('A').get('locked'))
        revision = account('A')['revision']
        send('A', 'storage', item=ammo['id'], quantity=29, to=0, **{'from': 1})
        check('Locked request cannot move items', lambda: account('A')['revision'] == revision and bool(account('A').get('message')))
        send('A', 'ready')
        check('Cancel ready unlocks inventory', lambda: not account('A')['locked'])
        return

    if scenario == 'recovery':
        profiles = {r: account(r)['profile'] for r in ['A', 'B']}
        position('A', [1750, 4600, 500]); position('B', [1900, 4600, 500])
        stolen = player('Server', 'B')['weapon_item']
        send('B', 'pulse', action='Drop')
        check('B drops original persistent rifle', lambda: not player('Server', 'B').get('weapon'))
        loot = next(box for box in state('Server')['loot'] if any(i['id'] == stolen for i in box['contents']['items']))
        position('B', [2400, 5000, 500])
        position('A', [loot['position'][0] - 70, loot['position'][1], loot['position'][2] + 90])
        send('A', 'open_loot', target=loot['id'])
        check('A takes B rifle into second weapon slot', lambda: any(i['id'] == stolen and i['slot'] == 1 for i in player('Server', 'A')['inventory']['items']))
        send('A', 'inventory', open=True)
        for item in [i.copy() for i in player('Server', 'A')['inventory']['items'] if i['definition'] == 'Ammo']:
            item_action('A', item['id'], kind='drop', quantity=item['quantity'])
            wait(lambda ident=item['id']: not any(i['id'] == ident for i in player('Server', 'A')['inventory']['items']), 'own ammo removed')
        send('A', 'inventory', open=False)
        position('B', [1900, 4200, 500])
        send('B', 'inventory', open=True)
        ammo = next(i for i in player('Server', 'B')['inventory']['items'] if i['definition'] == 'Ammo')
        previous = {box['id'] for box in state('Server')['loot']}
        item_action('B', ammo['id'], kind='drop', quantity=30)
        check('B splits and drops thirty original ammo units', lambda: any(box['id'] not in previous for box in state('Server')['loot']))
        send('B', 'inventory', open=False)
        box = next(box for box in state('Server')['loot'] if box['id'] not in previous)
        position('B', [2400, 5000, 500])
        position('A', [box['position'][0] - 70, box['position'][1], box['position'][2] + 90])
        send('A', 'open_loot', target=box['id'])
        check('A owns only thirty borrowed reserve rounds', lambda: player('Server', 'A')['inventory']['items'] and sum(i['quantity'] for i in player('Server', 'A')['inventory']['items'] if i['definition'] == 'Ammo') == 30)
        send('A', 'aim', target='', head=False)
        for _ in range(5):
            old = player('Server', 'A')['ammo']
            send('A', 'pulse', action='Fire')
            wait(lambda n=old: player('Server', 'A')['ammo'] == n - 1, 'shot before borrowed reload')
            time.sleep(.2)
        send('A', 'pulse', action='Reload')
        check('Borrowed ammo moves into original magazine', lambda: player('Server', 'A')['ammo'] == 30 and sum(i['quantity'] for i in player('Server', 'A')['inventory']['items'] if i['definition'] == 'Ammo') == 25)
        send('B', 'medical')
        check('Unsettled player consumes medicine before crash', lambda: sum(i['quantity'] for i in player('Server', 'B')['inventory']['items'] if i['definition'] == 'Medkit') == 1)
        enter_zone('A')
        if g['args'].unacked:
            check('Server committed success before client acknowledgement', lambda: (g['OUT'] / 'SettlementCommitted.txt').exists(), 25)
            check('Owner has not yet received success', lambda: account('A')['outcome'] == 0)
        else:
            extracted('A')
            send('A', 'screenshot', name='before-server-crash')

        def restart(number):
            previous_runs = g['report'].setdefault('crashed_processes', [])
            for role, proc in list(g['processes'].items()):
                record = dict(g['report']['processes'][role]); record['expected_server_restart'] = number
                if proc.poll() is None:
                    subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], capture_output=True)
                record['exit_code'] = proc.wait(timeout=30)
                previous_runs.append(record)
                log = g['OUT'] / (role + '.log')
                if log.exists(): shutil.copyfile(log, g['OUT'] / f'{role}-before-restart-{number}.log')
                for suffix in ['-state.json', '-command.json']:
                    (g['OUT'] / (role + suffix)).unlink(missing_ok=True)
                g['last_states'].pop(role, None); g['commands'].pop(role, None)
            g['processes'].clear()
            g['launch']('Server')
            for role in ['A', 'B']:
                g['launch'](role); g['join'](role)
                check(role + f' same authenticated profile after restart {number}', lambda r=role: account(r).get('bound') and account(r).get('profile') == profiles[r])

        restart(1)
        check('Successful profile retains both rifles', lambda: count('A', 'AK') == 4 and account('A')['outcome'] == 1)
        check('Unsettled player cannot duplicate the exported rifle', lambda: count('B', 'AK') == 2 and account('B')['outcome'] == 6)
        check('Stack and magazine exports both reduce crash refund', lambda: count('B', 'Ammo') == 330)
        check('Unexported consumed medical item is restored from load-in snapshot', lambda: count('B', 'Medkit') == 6)
        totals = {r: account(r) for r in ['A', 'B']}
        restart(2)
        check('Repeated restart is exactly once for both accounts', lambda: all(account(r)['stash'] == totals[r]['stash'] and account(r)['overflow'] == totals[r]['overflow'] for r in ['A', 'B']))
        send('B', 'stash_ui'); send('B', 'screenshot', name='recovered-stash')
        return

    if scenario == 'four':
        disconnected_profile = account('C')['profile']
        for role, xyz in {'B': [1400, 4200, 500], 'C': [1400, 3900, 500], 'D': [2400, 5000, 500]}.items(): position(role, xyz)
        enter_zone('A'); extracted('A')
        g['aim']('C', 'B', True); g['shot']('C', 'B', 40); g['shot']('C', 'B', 40)
        check('Second player is downed while fourth player remains alive', lambda: player('Server', 'B')['life'] == 5)
        for _ in range(8):
            send('C', 'pulse', action='Fire')
            time.sleep(.4)
            if player('Server', 'B').get('life') == 1: break
        check('Second player dies with independent failure result', lambda: account('B')['outcome'] == 2 and player('Server', 'B')['life'] == 1)
        g['finishing'].add('C'); g['report']['processes']['C']['expected_disconnect'] = True
        subprocess.run(['taskkill', '/PID', str(g['processes']['C'].pid), '/T', '/F'], capture_output=True)
        check('Disconnected third player leaves fourth active', lambda: state('Server')['alive'] == 1 and player('Server', 'D')['life'] == 0, 130)
        check('Disconnected profile has durable loss result', lambda: any(p['profile'] == disconnected_profile and p['outcome'] == 4 for p in state('Server')['stored_results']))
        enter_zone('D')
        send('Server', 'deadline_tie', target='EX_D')
        check('Remaining player receives timeout settlement', lambda: account('D')['outcome'] == 5 and player('Server', 'D')['life'] == 4)
        check('Prior successful and death results remain unchanged', lambda: account('A')['outcome'] == 1 and account('B')['outcome'] == 2)
