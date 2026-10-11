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
from pe_validation import validate_executable

sys.stdout.reconfigure(encoding='utf-8')
ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path('C:/UnrealProjects/UnrealEngine-5.8.3-release')
parser = argparse.ArgumentParser()
parser.add_argument('--tag', required=True)
parser.add_argument('--packaged', action='store_true')
parser.add_argument('--package-root', default='Packaged', help='Archive root containing the Client/Server packages')
parser.add_argument('--port', type=int, default=27777, help='Isolated test server port')
parser.add_argument('--isolated-room', action='store_true', help='Only join the room advertised with this run tag; requires the current driver')
parser.add_argument('--lag', action='store_true')
parser.add_argument('--unacked', action='store_true', help='Recovery test: kill server after durable extraction commit before client acknowledgement')
parser.add_argument('--debug-hold', action='store_true', help='Wait for Continue.txt before combat and extend assertion timeouts')
parser.add_argument('--traversal-record', action='store_true', help='Record owner and observer traversal clips')
parser.add_argument('--traversal-route-only', action='store_true', help='Run only the continuous source-map traversal route')
parser.add_argument('--traversal-source-only', action='store_true', help='Round trips on all enabled original climb actors; reject legacy furniture prompts')
parser.add_argument('--traversal-top-only', action='store_true', help='Focused top-entry prediction reproducer')
parser.add_argument('--traversal-debug-point', choices=['vault','ladder'], default='vault')
parser.add_argument('--traversal-route', action='store_true', help='Also run the continuous source-map traversal route')
parser.add_argument('--traversal-lifecycle', action='store_true', help='Three-player traversal damage, death and disconnect acceptance')
parser.add_argument('--optic-validation', action='store_true', help='Additional mounted-optic visual acceptance in the equipment scenario')
parser.add_argument('--optic-remote-only', action='store_true', help='Stop equipment acceptance after observer mount captures; not a full combat regression')
parser.add_argument('--resolution', choices=['1280x720', '1920x1080'], help='Rendered client size for visual acceptance')
parser.add_argument('--scenario', choices=['combat', 'spectator', 'timeout', 'load_timeout', 'disconnect', 'smoke', 'inventory', 'swap', 'settlement', 'recovery', 'four', 'identity', 'abilities', 'squad', 'pvpve', 'guard_tuning', 'lifecycle', 'equipment', 'progression', 'streaming', 'performance', 'northcombat', 'east', 'traversal'], default='combat')
parser.add_argument('--players', type=int, choices=[2, 3, 6])
parser.add_argument('--rendered-clients', type=int, choices=[1, 2, 3, 6], help='Other clients use NullRHI; report this as functional-only coverage')
parser.add_argument('--rendered-role', choices=['A', 'B'], help='Render only this client; the other clients remain connected with NullRHI')
parser.add_argument('--performance-route', choices=['old', 'north', 'east'], default='north')
parser.add_argument('--performance-samples', type=int, choices=[1, 3], default=3)
parser.add_argument('--performance-seconds', type=int, choices=[60, 180], default=180)
parser.add_argument('--network-stats', action='store_true', help='Enable UE network byte-rate collection for a separate bandwidth capture')
parser.add_argument('--streaming-loops', type=int, choices=[1, 2, 10], default=10)
args = parser.parse_args()
if not 1024 <= args.port <= 65535:
    parser.error('--port must be between 1024 and 65535')
if args.traversal_lifecycle and (args.scenario!='traversal' or args.players!=3):
    parser.error('--traversal-lifecycle requires --scenario traversal --players 3')
if args.traversal_source_only and (args.scenario!='traversal' or args.traversal_route_only or args.traversal_lifecycle):
    parser.error('--traversal-source-only requires a separate traversal round')
if not args.packaged and args.package_root != 'Packaged':
    parser.error('--package-root requires --packaged')
package_root = (ROOT / args.package_root).resolve()
if args.optic_validation and args.scenario != 'equipment':
    parser.error('--optic-validation requires --scenario equipment')
if args.rendered_role and args.rendered_clients:
    parser.error('Choose --rendered-role or --rendered-clients')
if args.optic_validation and (args.rendered_role == 'A' or args.rendered_clients == 1):
    parser.error('Optic pixel acceptance requires rendered client B')
if args.optic_remote_only and (args.scenario != 'equipment' or args.rendered_role != 'A' or args.optic_validation):
    parser.error('--optic-remote-only requires equipment with --rendered-role A and no --optic-validation')
if not args.tag or not all(c.isalnum() or c in '-_' for c in args.tag):
    raise ValueError('Use an alphanumeric run tag')
OUT = ROOT / 'Saved/Extraction' / args.tag
OUT.mkdir(parents=True, exist_ok=False)
ROLES = ['A', 'B', 'C', 'D'] if args.scenario == 'four' else ['A', 'B', 'C'] if args.scenario == 'spectator' else ['A', 'B']
if args.scenario in ['abilities', 'squad', 'pvpve', 'lifecycle', 'equipment', 'progression', 'streaming', 'performance', 'northcombat', 'east', 'traversal']:
    ROLES = list('ABCDEF')[:args.players or (6 if args.scenario in ['pvpve', 'lifecycle'] else 2 if args.scenario == 'traversal' else 3)]
rendered_roles = [args.rendered_role] if args.rendered_role else ROLES[:args.rendered_clients]
processes, commands, checks = {}, {}, []
last_states = {}
finishing = set()
report = {'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'options': vars(args), 'checks': checks, 'processes': {}, 'result': 'running'}
report['base_commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
report['input_sha256'] = {str(p.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in [Path(__file__).resolve(), ROOT / 'Source/BrokenEdge/Tests/ExtractionDriverTests.cpp',
                                   ROOT / 'Source/BrokenEdge/Extraction/ExtractionPlayerController.h',
                                   ROOT / 'Source/BrokenEdge/Extraction/ExtractionPlayerController.cpp',
                                   ROOT / 'Scripts/Extraction/inventory_scenarios.py',
                                   ROOT / 'Scripts/Extraction/storage_scenarios.py', ROOT / 'Scripts/Extraction/phase5_scenarios.py',
                                   ROOT / 'Scripts/Extraction/equipment_scenarios.py',
                                   ROOT / 'Scripts/Extraction/optic_scenarios.py',
                                   ROOT / 'Scripts/Extraction/verify_optic_capture.py',
                                   ROOT / 'Scripts/Extraction/optic_calibration.py',
                                   ROOT / 'Content/Extraction/Items/DA_Item_RedDot.uasset',
                                   ROOT / 'Scripts/Extraction/progression_scenarios.py',
                                   ROOT / 'Scripts/Extraction/streaming_scenarios.py',
                                   ROOT / 'Scripts/Extraction/north_combat_scenarios.py',
                                   ROOT / 'Scripts/Extraction/resource_guard.py',
                                   ROOT / 'Scripts/SpaceCenter/north_route.json',
                                   ROOT / 'Scripts/SpaceCenter/route.json',
                                   ROOT / 'Content/Maps/SpaceCenter/L_SpaceCenterNorth.umap',
                                   ROOT / 'BrokenEdge.uproject', ROOT / 'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap']}
if args.scenario == 'east' or args.scenario == 'performance' and args.performance_route == 'east':
    for name in ['Scripts/SpaceCenter/east_route.json','Scripts/Extraction/east_scenarios.py']:
        report['input_sha256'][name]=hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
if args.scenario == 'guard_tuning':
    for name in ['Scripts/Extraction/guard_tuning_scenarios.py', 'Config/DefaultGame.ini',
                 'Source/BrokenEdge/Extraction/ExtractionAbilities.h',
                 'Source/BrokenEdge/Extraction/ExtractionGuard.h',
                 'Source/BrokenEdge/Extraction/ExtractionGuard.cpp',
                 'Source/BrokenEdge/Extraction/ExtractionHealth.cpp']:
        report['input_sha256'][name] = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()


def save():
    (OUT / 'Report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


def read_snapshot(path):
    if os.name != 'nt':
        return path.read_text(encoding='utf-8-sig')
    # UE replaces this file atomically. CRT open() denies deletion and can stall
    # the game thread for FileManager's 0.5 s retry. Share deletion as well as I/O.
    import ctypes
    import msvcrt
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                  ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.CreateFileW(str(path), 0x80000000, 7, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError()
    try:
        fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        kernel.CloseHandle(handle)
        raise
    with os.fdopen(fd, 'r', encoding='utf-8-sig') as stream:
        return stream.read()


def state(role):
    try:
        last_states[role] = json.loads(read_snapshot(OUT / (role + '-state.json')))
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
    snapshots={r:state(r) for r in processes}
    if args.scenario=='traversal':
        # Keep motion evidence without repeatedly serializing every stash/container during subsecond actions.
        snapshots={r:{k:v for k,v in s.items() if k in ('role','map','phase','packet_lag','packet_loss','quality')} |
                     {'players':[{k:v for k,v in p.items() if k not in ('account','inventory','loot_view','progression')} for p in s.get('players',[])]}
                   for r,s in snapshots.items()}
    checks.append({'name': name, 'states': snapshots})
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
        executable = package_root / ('Server/WindowsServer/BrokenEdge/Binaries/Win64/BrokenEdgeServer.exe'
                                     if role == 'Server' else 'Client/Windows/BrokenEdge/Binaries/Win64/BrokenEdge.exe')
        command = [str(executable)]
        report.setdefault('executables',{})[role]=validate_executable(executable)
    else:
        command = [str(ENGINE / 'Engine/Binaries/Win64/UnrealEditor.exe'), str(ROOT / 'BrokenEdge.uproject'),
                   '-server' if role == 'Server' else '-game']
    entry = '/Game/Maps/SpaceCenter/L_SpaceCenterExtraction?ExpectedPlayers=3' if role == 'Server' and args.scenario == 'load_timeout' else '/Game/Maps/Lobby'
    command += [entry, '-nosound', '-unattended', '-nosplash', '-NoLoadingScreen',
                '-ExtractionGuards=' + ('4' if args.scenario in ['pvpve', 'guard_tuning', 'progression', 'northcombat'] or args.scenario == 'equipment' and len(ROLES) == 6 else '0'),
                '-MatchType=Extraction', '-NumPublicConnections=' + str(len(ROLES)),
                '-MinPlayersToStart=' + str(len(ROLES)), '-port=' + str(args.port), '-ExtractionTestPort=' + str(args.port),
                '-ExtractionRoundSeconds=' + ('15' if args.scenario == 'timeout' else '1200' if args.debug_hold or args.scenario in ['streaming', 'performance', 'east'] else '600'),
                '-ExtractionTestRole=' + role, '-ExtractionTestDir=' + str(OUT),
                '-ExtractionStore=' + str(OUT / 'Store'), '-ExtractionProfile=' + args.tag + '_' + ('A' if args.scenario == 'identity' and role == 'B' else role),
                '-ExecCmds=t.MaxFPS 30,' + ('sg.ShadowQuality 0,sg.EffectsQuality 1,' if len(ROLES) == 6 else '') + 'BrokenEdge.Debug.HitScan 1,Automation RunTests BrokenEdge.Extraction.ProcessDriver',
                '-TestExit=Automation Test Queue Empty', '-abslog=' + str(OUT / (role + '.log'))]
    if args.isolated_room:
        command += ['-RoomName=' + args.tag, '-ExtractionTestRoom=' + args.tag]
    rendered = role in rendered_roles
    if args.scenario in ['performance', 'traversal']:
        command = [c for c in command if not c.startswith('-ExecCmds=')]
        quality = ','.join('sg.' + name + 'Quality 1' for name in ['ViewDistance', 'AntiAliasing', 'Shadow', 'GlobalIllumination', 'Reflection', 'PostProcess', 'Texture', 'Effects', 'Foliage', 'Shading'])
        if args.scenario == 'traversal' and args.traversal_record:
            quality += ',r.MotionBlurQuality 0'
        cap = '0' if args.scenario == 'performance' else '30'
        command += ['-csvGpuStats', '-csvCategories=GPUMem', '-ExecCmds=' + ('t.MaxFPS '+cap+',r.VSync 0,r.ScreenPercentage 100,' + quality if rendered else 't.MaxFPS 30') + ',Automation RunTests BrokenEdge.Extraction.ProcessDriver']
    elif args.scenario == 'northcombat':
        command += ['-csvGpuStats', '-csvCategories=GPUMem']
    if args.network_stats:
        command = [c.replace('-ExecCmds=', '-ExecCmds=net.EnableNetStats 1,', 1) if c.startswith('-ExecCmds=') else c for c in command]
    width, height = (args.resolution or ('1920x1080' if args.scenario == 'performance' else '1280x720')).split('x')
    command += ['-RenderOffscreen', '-windowed', '-ResX=' + width, '-ResY=' + height] if rendered else ['-nullrhi']
    if rendered and (args.scenario == 'performance' or args.resolution):
        # GameEngine otherwise clamps offscreen windows to the virtual desktop.
        command += ['-ForceRes']
    if args.debug_hold and args.scenario == 'equipment':
        command += ['-ExtractionVisualCalibration']
    if args.debug_hold:
        command += ['-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ConnectionTimeout=600.0',
                    '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:InitialConnectTimeout=600.0']
    if role == 'Server' and args.unacked:
        command += ['-ExtractionHoldSettlement=' + str(OUT / 'SettlementCommitted.txt')]
    if role == 'Server' and args.scenario == 'lifecycle':
        # Observe transport disconnect while the 30-second downed deadline is
        # still active. This applies only to this isolated acceptance server.
        command += ['-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ConnectionTimeout=10.0']
    if role == 'Server' and args.traversal_lifecycle:
        command += ['-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ConnectionTimeout=10.0']
    if args.scenario == 'swap':
        command += ['-LogCmds=LogTemp Verbose']
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
    phase4 = args.scenario in ['settlement', 'recovery', 'four', 'abilities', 'squad', 'pvpve', 'guard_tuning', 'lifecycle', 'equipment', 'progression', 'streaming', 'performance', 'northcombat', 'east', 'traversal']
    for role in ROLES:
        check(role + ' persistent account ready', lambda r=role: state(r).get('account', {}).get('bound'))
        for definition in (['AK'] if phase4 else []) + ['Ammo', 'Ammo', 'Medkit', 'Medkit']:
            account = state(role)['account']
            item = next((i for i in account['stash']['items'] if i['definition'] == definition), None)
            if item is None:
                raise AssertionError(role + ' missing stored loadout ' + definition)
            revision = account['revision']
            send(role, 'storage', item=item['id'], quantity=item['quantity'], slot=0 if definition == 'AK' else -1)
            wait(lambda r=role, v=revision: state(r)['account']['revision'] > v, 'loadout transfer committed')
    if args.scenario == 'traversal':
        # Use an ordinary persisted second weapon to prove E still switches guns.
        account=state('A')['account']
        item=next(i for i in account['stash']['items'] if i['definition']=='AK')
        revision=account['revision']
        send('A','storage',item=item['id'],quantity=1,slot=1)
        wait(lambda: state('A')['account']['revision']>revision,'traversal second weapon loadout')
    if args.scenario in ['abilities', 'squad', 'pvpve', 'lifecycle', 'equipment']:
        from phase5_scenarios import lobby
        lobby(globals())
    if args.scenario in ['streaming','east'] and len(ROLES) == 2:
        send('B', 'squad', value=1)
        check('Streaming pair shares a squad for spectator acceptance', lambda: player('Server', 'B').get('squad') == 1)
    if args.scenario == 'traversal' and args.traversal_lifecycle:
        send('C', 'squad', value=1)
        check('Traversal casualty has a living squadmate', lambda: player('Server', 'C').get('squad') == player('Server', 'A').get('squad'))
    for role in ROLES:
        send(role, 'ready')
    if args.scenario in ['equipment', 'progression', 'northcombat']:
        # Arrange the comparison as soon as authority starts the round. Waiting
        # for all rendered clients first lets live AI wound a fresh spawn before
        # the test's position fixture, especially on the second travel.
        wait(lambda: state('Server').get('phase') == 1 and all(
            'Stinger' in player('Server', r).get('pawn_class', '') for r in ROLES),
             'equipment authority pawns ready', 120)
        send('Server', 'fixture', players=[{'name': 'EX_'+r, 'position': [5700+150*i, 5000, 180],
                                           'health': 100, 'shield': 0} for i,r in enumerate(ROLES)])
    check('All players loaded in Extraction', lambda: all(state(r).get('phase') == 1 for r in processes), 120)
    if args.scenario in ['pvpve', 'guard_tuning']:
        send('Server', 'fixture', players=[{'name': 'EX_'+r, 'position': [5700+150*i, 5000, 180], 'health': 100, 'shield': 0} for i,r in enumerate(ROLES)])
    check('Dedicated authority and Stinger pawns', lambda: state('Server').get('net_mode') == 1
          and state('Server').get('local_players') == 0 and state('Server').get('alive') == len(ROLES)
          and all('Stinger' in player('Server', r).get('pawn_class', '') for r in ROLES))
    for role in ROLES:
        # Clear the previous round's test aim lock before selecting a spawn pickup.
        send(role, 'aim', target='', head=False)
        time.sleep(.2)
        if not phase4:
            check(role + ' visible reachable pickup focus', lambda r=role: player(r, r).get('focus', 'None') != 'None')
            send(role, 'pulse', action='Equip')
        check(role + ' equipped AK', lambda r=role: 'AK47' in player('Server', r).get('weapon', ''))
    time.sleep(1)
    check('First-person backpack section hidden on each owner', lambda: all(player(r, r).get('fp_gear_visible') is False for r in ROLES))
    check('Server and owners keep both hands within two cm of weapon grips', lambda: all(
        player(peer, r).get('left_grip_distance', 999) <= 2 and player(peer, r).get('right_grip_distance', 999) <= 2
        for r in ROLES for peer in ['Server', r]))


def fixture(shields=0, cover=False):
    positions = {'A': [1400, 4200, 500], 'B': [2400, 4200, 500] if cover else [1400, 3900, 500], 'C': [1400, 4600, 500]}
    send('Server', 'fixture', players=[{'name': 'EX_' + r, 'position': positions[r], 'health': 80, 'shield': shields, 'clear_trauma': True} for r in ROLES])
    time.sleep(1)


def aim(shooter, target, head=False):
    send(shooter, 'aim', target='EX_' + target, head=head)
    time.sleep(.8)


def shot(shooter, target, damage):
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
        current = player('Server', target)
        hit = current.get('bullet_sequence', 0) > victim.get('bullet_sequence', 0)
        loss = current.get('hit_loss', 0) if hit else 0
        if loss not in [0, min(20, victim['health']), min(40, victim['health'])]:
            raise AssertionError(f'Invalid shot damage: {loss}')
        expected_loss = min(damage, victim['health'])
        # Bleeding can leave less health than the requested headshot damage.
        # Health loss is capped by remaining HP, but still require a real head
        # hit so a low-health body hit cannot satisfy the headshot sample.
        classification = damage != 40 or current.get('hit_bone', '').lower() == 'head'
        if hit and loss == expected_loss and classification:
            label = str(damage) if expected_loss == damage else str(expected_loss) + ' capped from ' + str(damage)
            check(target + ' direct bullet health damage ' + label, lambda: True)
            check(target + ' damage replication', lambda: all(abs(player(r, target).get('health', 0) - player('Server', target).get('health', 0)) <= 1 for r in processes))
            return
        checks.append({'name': 'Scatter attempt', 'shooter': shooter, 'target': target, 'loss': loss,
                       'before': victim, 'after': current, 'attempt': attempt})
        if current.get('life') != 0:
            raise AssertionError('Scatter killed target before requested classification')
        send('Server', 'fixture', players=[{'name': 'EX_' + target, 'position': victim['position'],
                                           'health': victim['health'], 'shield': victim['shield'], 'clear_trauma': True}])
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
            if args.scenario == 'identity' and role == 'A':
                check('First profile authenticated before duplicate connection', lambda: state('A').get('account', {}).get('bound'))
    if args.scenario not in ['load_timeout', 'identity', 'progression']:
        begin_round()
    if args.lag:
        for role in processes:
            send(role, 'lag')
        time.sleep(2)
        check('Packet simulation applied: 50ms each direction and 2 percent loss', lambda: all(state(r).get('packet_lag') == 50 and state(r).get('packet_loss') == 2 for r in processes))
    for role in rendered_roles:
        send(role, 'screenshot', name='spawn')
    if args.scenario == 'identity':
        check('Same credential cannot bind two simultaneous clients', lambda: not state('B').get('account', {}).get('bound') and bool(state('B').get('account', {}).get('message')))
        send('B', 'ready')
        check('Unbound duplicate cannot become ready', lambda: not player('Server', 'B')['ready'])
        account = state('A')['account']
        send('B', 'storage', item=account['stash']['items'][0]['id'], quantity=1)
        check('Duplicate cannot mutate authenticated inventory', lambda: state('A')['account']['revision'] == account['revision'] and state('A')['account']['stash'] == account['stash'])
        check('Duplicate login cannot regrant starter kits', lambda: len(state('Server')['stored_results']) == 1 and sum(i['quantity'] for i in state('A')['account']['stash']['items'] if i['definition'] == 'AK') == 3)
        send('B', 'screenshot', name='duplicate-profile-denied')
    elif args.scenario == 'traversal':
        from traversal_scenarios import run
        run(globals())
    elif args.scenario == 'east':
        from east_scenarios import run
        run(globals())
    elif args.scenario in ['streaming', 'performance']:
        from streaming_scenarios import run
        run(globals())
    elif args.scenario == 'northcombat':
        from north_combat_scenarios import run
        run(globals())
    elif args.scenario == 'progression':
        from progression_scenarios import run
        run(globals())
    elif args.scenario == 'equipment':
        from equipment_scenarios import run
        run(globals())
    elif args.scenario == 'guard_tuning':
        from guard_tuning_scenarios import run
        run(globals())
    elif args.scenario in ['abilities', 'squad', 'pvpve', 'lifecycle']:
        from phase5_scenarios import run
        run(globals())
    elif args.scenario in ['settlement', 'recovery', 'four']:
        from storage_scenarios import run
        run(globals())
    elif args.scenario == 'load_timeout':
        check('Missing third participant keeps the round waiting', lambda: all(state(r).get('phase') == 0 for r in processes))
        positions = {r: player('Server', r)['position'] for r in ROLES}
        send('A', 'pulse', action='Move', duration=1)
        send('A', 'invalid', kind='waiting')
        time.sleep(2)
        check('Waiting blocks movement and shooting', lambda: player('Server', 'A')['position'] == positions['A'] and player('Server', 'A')['accepted'] == 0 and all(player('Server', r)['disabled'] for r in ROLES))
        check('120 second loading timeout aborts', lambda: state('Server').get('phase') == 2, 130)
        check('Aborted load returns clients to lobby', lambda: all(state(r).get('map') == '/Game/Maps/Lobby' for r in processes), 35)
    elif args.scenario == 'disconnect':
        disconnected_items = {i['id']: i for i in player('Server', 'B')['inventory']['items']}
        finishing.add('B')
        report['processes']['B']['expected_disconnect'] = True
        subprocess.run(['taskkill', '/PID', str(processes['B'].pid), '/T', '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        check('Abrupt disconnection leaves survivor active', lambda: state('Server').get('alive') == 1 and state('A').get('phase') == 1, 130)
        check('Abrupt disconnect transfers every possession to exactly one container', lambda: len([box for box in state('Server')['loot'] if {i['id'] for i in box['contents']['items']} == set(disconnected_items)]) == 1)
        check('Disconnect preserves quantities and weapon magazine', lambda: all(i['quantity'] == disconnected_items[i['id']]['quantity'] and i['magazine'] == disconnected_items[i['id']]['magazine'] for box in state('Server')['loot'] for i in box['contents']['items'] if i['id'] in disconnected_items))
    elif args.scenario == 'timeout':
        check('Round deadline settles surviving players as timed out', lambda: state('Server').get('phase') == 2 and all(player('Server', r).get('life') == 4 for r in ROLES), 25)
        check('Returns to ready lobby after status interval', lambda: all(state(r).get('map') == '/Game/Maps/Lobby' for r in processes), 35)
        check('Ready flags reset in lobby', lambda: all(not p.get('ready') for p in state('Server')['players']))
        begin_round()
        check('Same session starts a second round', lambda: state('Server').get('phase') == 1)
    elif args.scenario == 'swap':
        fixture()
        send('Server', 'inventory_fixture', target='EX_A', clear=False, items=[{'definition': 'AK', 'slot': 1}])
        wait(lambda: player('A', 'A')['inventory'] == player('Server', 'A')['inventory'], 'secondary replicated')
        original = player('Server', 'A')['weapon_item']
        secondary = next(i['id'] for i in player('Server', 'A')['inventory']['items'] if i['slot'] == 1)
        if args.debug_hold:
            print('WAITING_FOR_DEBUGGER', OUT / 'Continue.txt', flush=True)
            wait(lambda: (OUT / 'Continue.txt').exists(), 'debugger continue marker')
        for index in range(6):
            expected = secondary if index % 2 == 0 else original
            send('A', 'swap')
            check('Swap ' + str(index + 1) + ' commits expected weapon', lambda: all(player(r, 'A').get('weapon_item') == expected for r in processes))
            wait(lambda: player('Server', 'A')['combat_state'] == 0, 'swap complete')
        possessions = {i['id'] for i in player('Server', 'A')['inventory']['items']}
        owner = next(w['owner'] for w in state('Server')['weapons'] if w['name'] == player('Server', 'A')['weapon'])
        remaining_weapons = {w['name'] for w in state('Server')['weapons'] if w['owner'] != owner}
        send('A', 'swap')
        send('A', 'leave')
        check('Leaving during swap preserves all possessions once', lambda: len([box for box in state('Server')['loot'] if {i['id'] for i in box['contents']['items']} == possessions]) == 1)
        time.sleep(2)
        check('Cancelled swap cannot recreate equipment after leaving', lambda: {w['name'] for w in state('Server')['weapons']} == remaining_weapons)
    elif args.scenario == 'inventory':
        from inventory_scenarios import run_inventory_suite
        run_inventory_suite(globals())
    elif args.scenario != 'smoke':
        if args.debug_hold:
            print('WAITING_FOR_DEBUGGER', OUT / 'Continue.txt', flush=True)
            wait(lambda: (OUT / 'Continue.txt').exists(), 'debugger continue marker')
        fixture(40, cover=True)
        aim('A', 'B')
        old_ammo = player('Server', 'A')['ammo']
        send('A', 'pulse', action='Fire')
        time.sleep(1)
        check('Scene cover blocks damage while consuming the shot', lambda: player('Server', 'A')['ammo'] == old_ammo - 1 and player('Server', 'B')['health'] == 80)
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
        shot('A', 'B', 20)
        check('Extraction bullet damage bypasses legacy shield', lambda: player('Server', 'B')['shield'] == 40)
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
        weapon = player('Server', 'A')['weapon_item']
        send('A', 'pulse', action='Reload')
        check('Partial reload started before drop', lambda: player('Server', 'A')['combat_state'] == 1)
        send('A', 'pulse', action='Drop')
        check('Drop is authoritative', lambda: not player('Server', 'A').get('weapon'))
        time.sleep(4)
        check('Drop cancels unfinished reload and preserves weapon identity', lambda: any(i['id'] == weapon and i['magazine'] == 10 for w in state('Server')['loot'] for i in w['contents']['items']))
        dropped = next(w for w in state('Server')['loot'] if any(i['id'] == weapon for i in w['contents']['items']))
        position = dropped['position'].copy()
        position[2] = player('Server', 'A')['position'][2] + 20
        send('Server', 'fixture', players=[{'name': 'EX_A', 'position': position, 'health': 80, 'shield': 0}])
        check('Dropped weapon can be picked up', lambda: player('A', 'A').get('focus', 'None') != 'None')
        send('A', 'pulse', action='Equip')
        check('Re-equipped after interrupted reload', lambda: bool(player('Server', 'A').get('weapon')))
        aim('B', 'A', True)
        shot('B', 'A', 40)
        send('Server', 'fixture', players=[{'name': 'EX_A', 'position': player('Server', 'A')['position'],
                                           'health': 20, 'shield': 0, 'clear_trauma': True}])
        aim('B', 'A')
        death_weapon = player('Server', 'A')['weapon_item']
        send('A', 'pulse', action='Reload')
        check('Reload active immediately before lethal shot', lambda: player('Server', 'A')['combat_state'] == 1)
        shot('B', 'A', 20)
        if args.scenario == 'spectator':
            check('Lethal hit downs player while their squadmate lives', lambda: player('Server', 'A').get('life') == 5)
            for _ in range(8):
                send('B', 'pulse', action='Fire')
                time.sleep(.4)
                if player('Server', 'A').get('life') == 1:
                    break
            check('Finishing shots finalize downed player', lambda: player('Server', 'A').get('life') == 1)
        check('Death enters spectator without pawn', lambda: state('A').get('spectating') and not state('A').get('has_pawn'), 15)
        send('A', 'restart')
        time.sleep(5)
        check('Restart cannot respawn the dead player', lambda: not state('A').get('has_pawn') and player('Server', 'A').get('life') == 1)
        check('Death container preserves weapon and cancels its reload', lambda: any(i['id'] == death_weapon and i['magazine'] == 10 for w in state('Server')['loot'] for i in w['contents']['items']))
        send('B', 'aim', target='', head=False)
        time.sleep(.4)
        send('A', 'screenshot', name='spectator')
        check('Living player keeps round active', lambda: state('Server').get('phase') == 1 and player('Server', 'B').get('life') == 0)
        if args.scenario == 'spectator':
            check('Dead player spectates only their living squadmate', lambda: state('A').get('spectated') == 'EX_C')
            previous = state('A')['spectated']
            send('A', 'cycle', direction=1)
            time.sleep(.5)
            check('Next target wraps within the one living squadmate', lambda: state('A')['spectated'] == previous)
            send('A', 'cycle', direction=-1)
            time.sleep(.5)
            check('Previous target cannot select the opposing squad', lambda: state('A')['spectated'] == previous)
            send('Server', 'fixture', players=[{'name': 'EX_C', 'position': [1400, 4200, 500], 'health': 80, 'shield': 0}])
            aim('C', 'B', True)
            shot('C', 'B', 40)
            # Bleeding can tick between the pre-shot snapshot and the bullet.
            # Isolate this legacy spectator fixture's final direct-damage check.
            send('Server', 'fixture', players=[{'name': 'EX_B', 'position': player('Server', 'B')['position'],
                                               'health': 40, 'shield': 0, 'clear_trauma': True}])
            check('Finishing-shot fixture isolates direct damage from bleeding ticks', lambda: all(
                player(r, 'B').get('health') == 40 and not player(r, 'B').get('bleeding') for r in processes))
            shot('C', 'B', 40)
            check('Enemy death preserves the friendly spectator target', lambda: state('A')['spectated'] == 'EX_C')
            send('C', 'leave')
            check('Leaving survivor returns to room browser', lambda: state('C').get('map') == '/Game/Maps/Lobby' and not state('C').get('session'))
            check('No survivors ends round and fixes spectator camera', lambda: state('Server').get('phase') == 2 and state('A').get('spectated') == '')
            send('A', 'screenshot', name='no-survivors')
            check('Dead spectators return to ready lobby', lambda: all(state(r).get('map') == '/Game/Maps/Lobby' for r in processes), 35)
            join('C')
            begin_round()
            check('Former spectators can play the next round', lambda: all(player('Server', r).get('life') == 0 for r in ROLES))
        else:
            check('Dead solo squad has no enemy spectator target', lambda: state('A').get('spectated') == '')
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
        report['processes'][role]['finish_command'] = commands[role]
        report['processes'][role]['finish_ack'] = state(role).get('command') == commands[role]
    for role, proc in processes.items():
        code = proc.wait(timeout=40)
        report['processes'][role]['exit_code'] = code
        text = (OUT / (role + '.log')).read_text(encoding='utf-8', errors='replace')
        if report['processes'][role].get('expected_disconnect'):
            continue
        # TestExit can terminate after UE's Success line but before all buffered
        # AddInfo events reach disk. The finish branch writes this exact command
        # acknowledgement immediately before returning true; require it as well
        # as UE's completed-test result and the process exit code.
        record = report['processes'][role]
        record['finish_info_logged'] = 'EXTRACTION_DRIVER_OK' in text
        record['automation_success'] = bool(re.search(r'Test Completed\. Result=\{(?:Success|\u6210\u529f)\}', text))
        if code != 0 or not record.get('finish_ack') or 'Fatal error:' in text or not record['automation_success']:
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
