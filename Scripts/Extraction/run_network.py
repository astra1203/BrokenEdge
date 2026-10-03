"""Independent Development processes, production UI/input/RPCs and recorded assertions.

Fixtures only arrange position, health and ammo on the server. Shots use Enhanced
Input; deliberately invalid packets exercise the same production RPC endpoints.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import time
import traceback

sys.stdout.reconfigure(encoding='utf-8')
ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path('C:/UnrealProjects/UnrealEngine-5.8.3-release')
parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True)
parser.add_argument('--packaged', action='store_true')
parser.add_argument('--lag', action='store_true')
parser.add_argument('--debug-hold', action='store_true', help='Wait for Continue.txt before combat and extend assertion timeouts')
parser.add_argument('--scenario', choices=['combat', 'spectator', 'timeout', 'load_timeout', 'disconnect', 'smoke'], default='combat')
args = parser.parse_args()
if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
    raise ValueError('Use an alphanumeric run tag')
OUT = ROOT / 'Saved/Extraction' / args.tag
OUT.mkdir(parents=True, exist_ok=False)
ROLES = ['A', 'B', 'C'] if args.scenario == 'spectator' else ['A', 'B']
processes, commands, checks = {}, {}, []
last_states = {}
finishing = set()
report = {'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'options': vars(args), 'checks': checks, 'processes': {}, 'result': 'running'}
report['base_commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
report['input_sha256'] = {str(p.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in [Path(__file__).resolve(), ROOT / 'Source/BrokenEdge/Tests/ExtractionDriverTests.cpp',
                                   ROOT / 'BrokenEdge.uproject', ROOT / 'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap']}


def save():
    (OUT / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


def state(role):
    try:
        last_states[role] = json.loads((OUT / (role + '-state.json')).read_text(encoding='utf-8-sig'))
    except (OSError, ValueError):
        pass
    # Windows replacement briefly removes the destination while UE retries a
    # shared read handle. Keep the previous complete snapshot until the next one.
    return last_states.get(role, {})


def player(role, name):
    return next((p for p in state(role).get('players', []) if p['name'] == 'EX_' + name), {})


def wait(predicate, message, timeout=45):
    if args.debug_hold:
        timeout = max(timeout, 300)
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        for role, proc in processes.items():
            if proc.poll() is not None and role not in finishing:
                raise RuntimeError(f'{role} exited {proc.returncode}: {message}')
        time.sleep(.15)
    raise TimeoutError(message)


def check(name, predicate, timeout=45):
    wait(predicate, name, timeout)
    checks.append({'name': name, 'states': {r: state(r) for r in processes}})
    save()
    print('PASS', name, flush=True)


def send(role, op, **fields):
    ident = commands.get(role, 0) + 1
    commands[role] = ident
    file = OUT / (role + '-command.json')
    temp = file.with_suffix('.tmp')
    temp.write_text(json.dumps({'id': ident, 'op': op, **fields}), encoding='utf-8')
    # UE's brief Windows read handle can temporarily deny replacement.
    for attempt in range(100):
        try:
            os.replace(temp, file)
            break
        except PermissionError:
            if attempt == 99:
                raise
            time.sleep(.01)
    wait(lambda: state(role).get('command', 0) >= ident, f'{role}: {op}', 30)
    return state(role).get('error', '')


def launch(role):
    if args.packaged:
        executable = ROOT / ('Packaged/Server/WindowsServer/BrokenEdge/Binaries/Win64/BrokenEdgeServer.exe'
                             if role == 'Server' else 'Packaged/Client/Windows/BrokenEdge/Binaries/Win64/BrokenEdge.exe')
        command = [str(executable)]
    else:
        command = [str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor.exe'), str(ROOT / 'BrokenEdge.uproject'),
                   '-server' if role == 'Server' else '-game']
    entry = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction?ExpectedPlayers=3' if role == 'Server' and args.scenario == 'load_timeout' else '/Game/Maps/Lobby'
    command += [entry, '-nosound', '-unattended', '-nosplash', '-NoLoadingScreen',
                '-MatchType=Extraction', '-NumPublicConnections=' + str(len(ROLES)),
                '-MinPlayersToStart=' + str(len(ROLES)), '-port=27777',
                '-ExtractionRoundSeconds=' + ('15' if args.scenario == 'timeout' else '600'),
                '-ExtractionTestRole=' + role, '-ExtractionTestDir=' + str(OUT),
                '-ExecCmds=BrokenEdge.Debug.HitScan 1,Automation RunTests BrokenEdge.Extraction.ProcessDriver',
                '-TestExit=Automation Test Queue Empty', '-abslog=' + str(OUT / (role + '.log'))]
    command += ['-nullrhi'] if role == 'Server' else ['-RenderOffscreen', '-windowed', '-ResX=1280', '-ResY=720']
    env = os.environ.copy()
    # The SDK was verified by the recorded build; prevent editor startup from
    # waiting on the build mutex just to repeat platform SDK discovery.
    env['UE_SKIP_UBT_SDK_SETUP'] = '1'
    proc = subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    processes[role] = proc
    report['processes'][role] = {'pid': proc.pid, 'command': command, 'environment_overrides': {'UE_SKIP_UBT_SDK_SETUP': '1'}}
    save()
    wait(lambda: bool(state(role)), role + ' driver ready', 120)
    print('READY', role, proc.pid, flush=True)


def join(role):
    for _ in range(20):
        if state(role).get('net_mode') == 3 and state(role).get('session'):
            break
        send(role, 'join')
        time.sleep(1)
    check(role + ' joined LAN lobby', lambda: state(role).get('net_mode') == 3 and state(role).get('session'))
    send(role, 'name')
    check(role + ' name replicated', lambda: bool(player('Server', role)))


def begin_round():
    for role in ROLES:
        send(role, 'ready')
    check('All players loaded in Extraction', lambda: all(state(r).get('phase') == 1 for r in processes), 120)
    check('Dedicated authority and Stinger pawns', lambda: state('Server').get('net_mode') == 1
          and state('Server').get('local_players') == 0 and state('Server').get('alive') == len(ROLES)
          and all('Stinger' in player('Server', r).get('pawn_class', '') for r in ROLES))
    for role in ROLES:
        check(role + ' real pickup overlap', lambda r=role: bool(player(r, r).get('overlap')))
        send(role, 'pulse', action='Equip')
        check(role + ' equipped AK', lambda r=role: 'AK47' in player('Server', r).get('weapon', ''))
    time.sleep(1)
    check('First-person backpack section hidden on each owner', lambda: all(player(r, r).get('fp_gear_visible') is False for r in ROLES))
    check('Server and owners keep both hands within two cm of weapon grips', lambda: all(
        player(peer, r).get('left_grip_distance', 999) <= 2 and player(peer, r).get('right_grip_distance', 999) <= 2
        for r in ROLES for peer in ['Server', r]))


def fixture(shields=0, cover=False):
    positions = {'A': [1400, 4200, 500], 'B': [2400, 4200, 500] if cover else [1400, 3900, 500], 'C': [1400, 4600, 500]}
    send('Server', 'fixture', players=[{'name': 'EX_' + r, 'position': positions[r], 'health': 80, 'shield': shields} for r in ROLES])
    time.sleep(1)


def aim(shooter, target, head=False):
    send(shooter, 'aim', target='EX_' + target, head=head)
    time.sleep(.8)


def shot(shooter, target, damage, shield=False):
    # Production AK scatter is retained. Every attempt must consume exactly one
    # round on every peer. An unintended body hit/miss is recorded, and the target
    # fixture is restored before retrying the requested damage classification.
    for attempt in range(12):
        old = player('Server', shooter).copy()
        victim = player('Server', target).copy()
        send(shooter, 'pulse', action='Fire')
        check(shooter + ' one authoritative shot', lambda: player('Server', shooter).get('accepted') == old['accepted'] + 1)
        check(shooter + ' ammo agreement', lambda: all(player(r, shooter).get('ammo') == old['ammo'] - 1 for r in processes))
        time.sleep(.35)
        key = 'shield' if shield else 'health'
        current = player('Server', target)
        loss = victim['health'] + victim['shield'] - current.get('health', 0) - current.get('shield', 0)
        if loss not in [0, min(20, victim['health'] + victim['shield']), min(40, victim['health'] + victim['shield'])]:
            raise AssertionError(f'Invalid shot damage: {loss}')
        if current.get(key) == victim[key] - damage:
            check(target + ' ' + key + ' damage ' + str(damage), lambda: True)
            check(target + ' damage replication', lambda: all(player(r, target).get(key) == victim[key] - damage for r in processes))
            return
        checks.append({'name': 'Scatter attempt', 'shooter': shooter, 'target': target, 'loss': loss,
                       'before': victim, 'after': current, 'attempt': attempt})
        if current.get('life') != 0:
            raise AssertionError('Scatter killed target before requested classification')
        send('Server', 'fixture', players=[{'name': 'EX_' + target, 'position': victim['position'],
                                           'health': victim['health'], 'shield': victim['shield']}])
        wait(lambda: all(player(r, target).get('health') == victim['health'] and player(r, target).get('shield') == victim['shield'] for r in processes), 'fixture restored')
        time.sleep(.4)
    raise AssertionError('No requested hit after 12 scatter attempts')


try:
    launch('Server')
    for role in ROLES:
        launch(role)
        if args.scenario == 'load_timeout':
            send(role, 'direct_join')
            wait(lambda r=role: state(r).get('net_mode') == 3 and state(r).get('has_pawn'), 'direct waiting map join', 60)
            send(role, 'name')
            wait(lambda r=role: bool(player('Server', r)), 'waiting player name')
        else:
            join(role)
    if args.scenario != 'load_timeout':
        begin_round()
    if args.lag:
        for role in processes:
            send(role, 'lag')
        time.sleep(2)
        check('Packet simulation applied: 50ms each direction and 2 percent loss', lambda: all(state(r).get('packet_lag') == 50 and state(r).get('packet_loss') == 2 for r in processes))
    for role in ROLES:
        send(role, 'screenshot', name='spawn')
    if args.scenario == 'load_timeout':
        check('Missing third participant keeps the round waiting', lambda: all(state(r).get('phase') == 0 for r in processes))
        positions = {r: player('Server', r)['position'] for r in ROLES}
        send('A', 'pulse', action='Move', duration=1)
        send('A', 'invalid', kind='waiting')
        time.sleep(2)
        check('Waiting blocks movement and shooting', lambda: player('Server', 'A')['position'] == positions['A'] and player('Server', 'A')['accepted'] == 0 and all(player('Server', r)['disabled'] for r in ROLES))
        check('120 second loading timeout aborts', lambda: state('Server').get('phase') == 2, 130)
        check('Aborted load returns clients to lobby', lambda: all(state(r).get('map') == '/Game/Maps/Lobby' for r in processes), 35)
    elif args.scenario == 'disconnect':
        finishing.add('B')
        report['processes']['B']['expected_disconnect'] = True
        subprocess.run(['taskkill', '/PID', str(processes['B'].pid), '/T', '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        check('Abrupt disconnection leaves survivor active', lambda: state('Server').get('alive') == 1 and state('A').get('phase') == 1, 130)
    elif args.scenario == 'timeout':
        check('Round deadline ends with living players', lambda: state('Server').get('phase') == 2 and state('Server').get('alive') == 2, 25)
        check('Returns to ready lobby after status interval', lambda: all(state(r).get('map') == '/Game/Maps/Lobby' for r in processes), 35)
        check('Ready flags reset in lobby', lambda: all(not p.get('ready') for p in state('Server')['players']))
        begin_round()
        check('Same session starts a second round', lambda: state('Server').get('phase') == 1)
    elif args.scenario != 'smoke':
        if args.debug_hold:
            print('WAITING_FOR_DEBUGGER', OUT / 'Continue.txt', flush=True)
            wait(lambda: (OUT / 'Continue.txt').exists(), 'debugger continue marker')
        fixture(40, cover=True)
        aim('A', 'B')
        old_ammo = player('Server', 'A')['ammo']
        send('A', 'pulse', action='Fire')
        time.sleep(1)
        check('Scene cover blocks damage while consuming the shot', lambda: player('Server', 'A')['ammo'] == old_ammo - 1 and player('Server', 'B')['shield'] == 40)
        fixture(40)
        aim('A', 'B')
        check('Visible remote character keeps both weapon grips', lambda: player('A', 'B').get('left_grip_distance', 999) <= 2 and player('A', 'B').get('right_grip_distance', 999) <= 2)
        send('A', 'pulse', action='Aim', duration=2)
        time.sleep(.6)
        send('A', 'screenshot', name='ads')
        time.sleep(1.6)
        send('A', 'pulse', action='ToggleCameraView')
        check('Third-person view toggle', lambda: not player('A', 'A').get('first_person'))
        send('A', 'screenshot', name='third-person')
        send('A', 'pulse', action='ToggleCameraView')
        check('First-person view restored', lambda: player('A', 'A').get('first_person'))
        aim('A', 'B')
        shot('A', 'B', 20, shield=True)
        shot('A', 'B', 20, shield=True)
        shot('A', 'B', 20)
        aim('A', 'B', True)
        shot('A', 'B', 40)
        fixture()
        old = player('Server', 'A').copy()
        for kind in ['duplicate', 'foreign', 'legacy', 'rewind', 'range']:
            send('A', 'invalid', kind=kind, target='EX_B')
        time.sleep(1)
        check('Duplicate foreign legacy rewind and invalid range cannot damage or spend ammo', lambda: player('Server', 'A')['ammo'] == old['ammo'] and player('Server', 'B')['health'] == 80)
        send('A', 'burst', count=12)
        time.sleep(1)
        check('Server cadence accepts one of twelve immediate requests', lambda: player('Server', 'A')['accepted'] == old['accepted'] + 1 and player('Server', 'A')['rejected'] >= old['rejected'] + 14)
        check('Rejected prediction corrected', lambda: all(player(r, 'A').get('ammo') == old['ammo'] - 1 for r in processes))
        send('A', 'pulse', action='Reload')
        check('Reload entered on server', lambda: player('Server', 'A')['combat_state'] == 1)
        send('A', 'invalid', kind='reloading')
        check('Reload fills once on all processes', lambda: all(player(r, 'A').get('ammo') == 30 for r in processes), 15)
        time.sleep(1)
        check('Reload terminates', lambda: all(player(r, 'A').get('combat_state') == 0 for r in processes))
        carried = player('Server', 'A')['carried_ammo']
        send('Server', 'ammo', target='EX_A', value=0)
        wait(lambda: all(player(r, 'A').get('ammo') == 0 for r in processes), 'empty ammo fixture replicated')
        accepted = player('Server', 'A')['accepted']
        send('A', 'invalid', kind='empty')
        time.sleep(.5)
        check('Empty weapon request rejected', lambda: player('Server', 'A')['accepted'] == accepted and player('Server', 'A')['ammo'] == 0)
        send('A', 'pulse', action='Reload')
        check('Empty reload transfers exactly thirty rounds', lambda: player('Server', 'A')['ammo'] == 30 and player('Server', 'A')['carried_ammo'] == carried - 30, 15)
        check('Owner reserve matches authority', lambda: player('A', 'A')['carried_ammo'] == carried - 30)
        send('Server', 'ammo', target='EX_A', value=10)
        wait(lambda: player('A', 'A').get('ammo') == 10, 'partial ammo fixture')
        weapon = player('Server', 'A')['weapon']
        send('A', 'pulse', action='Reload')
        check('Partial reload started before drop', lambda: player('Server', 'A')['combat_state'] == 1)
        send('A', 'pulse', action='Drop')
        check('Drop is authoritative', lambda: not player('Server', 'A').get('weapon'))
        time.sleep(4)
        check('Drop cancels unfinished reload', lambda: any(w['name'] == weapon and w['ammo'] == 10 and w['owner'] == 'None' for w in state('Server')['weapons']))
        dropped = next(w for w in state('Server')['weapons'] if w['name'] == weapon)
        position = dropped['position'].copy()
        position[2] = player('Server', 'A')['position'][2] + 20
        send('Server', 'fixture', players=[{'name': 'EX_A', 'position': position, 'health': 80, 'shield': 0}])
        check('Dropped weapon can be picked up', lambda: bool(player('A', 'A').get('overlap')))
        send('A', 'pulse', action='Equip')
        check('Re-equipped after interrupted reload', lambda: bool(player('Server', 'A').get('weapon')))
        aim('B', 'A', True)
        shot('B', 'A', 40)
        send('Server', 'fixture', players=[{'name': 'EX_A', 'position': player('Server', 'A')['position'], 'health': 20, 'shield': 0}])
        aim('B', 'A')
        death_weapon = player('Server', 'A')['weapon']
        send('A', 'pulse', action='Reload')
        check('Reload active immediately before lethal shot', lambda: player('Server', 'A')['combat_state'] == 1)
        shot('B', 'A', 20)
        check('Death enters spectator without pawn', lambda: state('A').get('spectating') and not state('A').get('has_pawn'), 15)
        send('A', 'restart')
        time.sleep(5)
        check('Restart cannot respawn the dead player', lambda: not state('A').get('has_pawn') and player('Server', 'A').get('life') == 1)
        check('Death drops weapon and cancels its reload', lambda: any(w['name'] == death_weapon and w['ammo'] == 10 and w['owner'] == 'None' for w in state('Server')['weapons']))
        send('B', 'aim', target='', head=False)
        time.sleep(.4)
        send('A', 'screenshot', name='spectator')
        check('Living player keeps round active', lambda: state('Server').get('phase') == 1 and state('A').get('spectated') in ['EX_B', 'EX_C'])
        if args.scenario == 'spectator':
            previous = state('A')['spectated']
            send('A', 'cycle', direction=1)
            check('Next living spectator target', lambda: state('A')['spectated'] != previous)
            send('A', 'cycle', direction=-1)
            check('Previous living spectator target', lambda: state('A')['spectated'] == previous)
            send('Server', 'fixture', players=[{'name': 'EX_C', 'position': [1400, 4200, 500], 'health': 80, 'shield': 0}])
            aim('C', 'B', True)
            shot('C', 'B', 40)
            shot('C', 'B', 40)
            check('Dead target automatically switches to survivor', lambda: state('A')['spectated'] == 'EX_C')
            send('C', 'leave')
            check('Leaving survivor returns to room browser', lambda: state('C').get('map') == '/Game/Maps/Lobby' and not state('C').get('session'))
            check('No survivors ends round and fixes spectator camera', lambda: state('Server').get('phase') == 2 and state('A').get('spectated') == '')
            send('A', 'screenshot', name='no-survivors')
            check('Dead spectators return to ready lobby', lambda: all(state(r).get('map') == '/Game/Maps/Lobby' for r in processes), 35)
            join('C')
            begin_round()
            check('Former spectators can play the next round', lambda: all(player('Server', r).get('life') == 0 for r in ROLES))
        else:
            send('A', 'leave')
            check('Leave cleans local session', lambda: state('A').get('map') == '/Game/Maps/Lobby' and not state('A').get('session'))
            check('Other player remains active', lambda: state('B').get('phase') == 1 and state('Server').get('alive') == 1)
            launch('C')
            send('C', 'direct_join', expect_rejection=True)
            check('Direct late join is rejected by the server', lambda: 'MATCH_IN_PROGRESS' in (OUT / 'C.log').read_text(encoding='utf-8', errors='replace'), 35)
            check('Rejected late join leaves the active round intact', lambda: state('Server').get('alive') == 1 and state('B').get('phase') == 1)
    # The automation driver must finish normally, not merely be killed after assertions.
    for role in sorted(processes, key=lambda r: r == 'Server'):
        if report['processes'][role].get('expected_disconnect'):
            continue
        finishing.add(role)
        send(role, 'finish')
    for role, proc in processes.items():
        code = proc.wait(timeout=40)
        report['processes'][role]['exit_code'] = code
        text = (OUT / (role + '.log')).read_text(encoding='utf-8', errors='replace')
        if report['processes'][role].get('expected_disconnect'):
            continue
        if code != 0 or 'EXTRACTION_DRIVER_OK' not in text or 'Fatal error:' in text or not re.search(r'Test Completed\. Result=\{(?:Success|\u6210\u529f)\}', text):
            raise RuntimeError(f'{role} driver did not finish normally ({code})')
    report['result'] = 'passed'
except Exception as error:
    report['result'] = 'failed'
    report['error'] = repr(error)
    report['traceback'] = traceback.format_exc()
    report['failure_states'] = {r: state(r) for r in processes}
    print('FAILED', repr(error), flush=True)
finally:
    for role, proc in processes.items():
        if proc.poll() is None:
            subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()
    save()
print('RESULT', report['result'], len(checks), OUT, flush=True)
sys.exit(0 if report['result'] == 'passed' else 1)
