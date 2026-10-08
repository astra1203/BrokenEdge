"""Production input/CMC traversal acceptance on deterministic replicated geometry."""
import hashlib
import math
from pathlib import Path
import time
import json
import threading


class MotionTrace:
    def __init__(self,g):
        self.g=g
        self.stop=threading.Event()
        self.thread=threading.Thread(target=self.capture,daemon=True)
        self.thread.start()

    def capture(self):
        last={}
        with (self.g['OUT']/'TraversalMotion.jsonl').open('w',encoding='utf-8') as output:
            while not self.stop.wait(.02):
                for peer in tuple(self.g['processes']):
                    state=self.g['state'](peer)
                    stamp=state.get('world_time')
                    if stamp is None or last.get(peer)==stamp:continue
                    last[peer]=stamp
                    row=dict(peer=peer,world_time=stamp,frame_delta=state.get('frame_delta'),
                             cap=state.get('quality',{}).get('t.MaxFPS'),
                             players=[{k:v for k,v in p.items() if k in ('name','position','falling','traversal','traversal_serial','traversal_elapsed','traversal_rejection')}
                                      for p in state.get('players',[])])
                    output.write(json.dumps(row)+'\n')

    def close(self):
        self.stop.set()
        self.thread.join(timeout=3)


def run(g):
    trace=MotionTrace(g)
    try:
        if not g['args'].traversal_route_only:run_cases(g)
        if g['args'].traversal_route or g['args'].traversal_route_only:
            from traversal_route import run as run_route
            run_route(g)
    finally:
        trace.close()


def run_cases(g):
    send, check, wait, state, player = (g[k] for k in ('send','check','wait','state','player'))
    g['report']['input_sha256']['Scripts/Extraction/traversal_scenarios.py'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    peers = ['Server','A','B']
    send('Server','traversal_fixture')
    time.sleep(2)
    half = player('Server','A')['capsule_half']
    g['report']['capsule'] = dict(half=half,radius=player('Server','A')['capsule_radius'])

    def place(role,x,y,z=0,yaw=0,health=100):
        send('Server','fixture',players=[dict(name='EX_'+role,position=[30000+x,30000+y,6000+z+half+3],health=health,shield=0,clear_trauma=True,yaw=yaw)])
        send(role,'look',yaw=yaw)
        wait(lambda: all(abs(player(p,role)['position'][0]-(30000+x))<5 for p in ('Server',role)), 'fixture position converges')
        time.sleep(.4)

    def press(key, seconds=.08, role='A'):
        # Offscreen viewports do not receive focus-dependent physical key mappings.
        # Inject the same Enhanced Input action used by the production binding.
        action={'SpaceBar':'Jump','E':'Equip','W':'Move','S':'MoveBack'}[key]
        send(role,'pulse',action=action,duration=seconds)
        time.sleep(seconds+.08)

    def climb_to_exit(action):
        send('A','pulse',action=action,duration=4)
        wait(lambda: player('Server','A')['traversal'] in (5,0),'ladder reaches exit transition',5)
        # Release on the exit transition so held walking input cannot run off the roof after landing.
        send('A','pulse',action=action,duration=0)

    place('B',-550,400)
    if g['args'].traversal_top_only:
        place('A',90,-900,250,yaw=180)
        if g['args'].debug_hold:wait(lambda: (g['OUT']/'Continue.txt').exists(),'top entry debugger prepared',900)
        press('E')
        check('Focused top entry accepted by authority',lambda: player('Server','A')['traversal']==4)
        return
    recording=g['args'].traversal_record

    def record(name,y,enabled):
        if not recording:return
        if enabled:
            send('B','world_camera',position=[29480,30000+y-210,6360],target=[30000,30000+y,6220],field_of_view=70)
        for role in ('A','B'):
            send(role,'record',enabled=enabled,name=name)

    results=[]
    for fps in (30,60,120):
        send('A','fps',value=fps)
        for y,kind,height in ((0,1,60),(350,1,90),(700,1,120),(1050,2,180)):
            place('A',-110,y)
            if g['args'].debug_hold and not results:
                wait(lambda: (g['OUT']/'Continue.txt').exists(),'traversal debugger prepared',900)
            start=player('Server','A')['traversal_serial']
            if fps==30:record(str(height),y,True)
            before=time.monotonic()
            press('SpaceBar',.035)
            check(f'{fps} fps {height} cm starts kind {kind}', lambda: player('Server','A')['traversal']==kind,3)
            observed={p:player(p,'A')['traversal'] for p in peers}
            # Readbacks are restricted to the separate video pass, so frame-cap runs keep their actual cadence.
            wait(lambda: player('Server','A')['traversal']==0,'traversal completes',5)
            check(f'{fps} fps {height} cm landed and converged',lambda: all(player(p,'A')['traversal']==0 and not player(p,'A')['falling'] and math.dist(player(p,'A')['position'],player('Server','A')['position'])<5 for p in peers),5)
            end=player('Server','A')['position']
            check(f'{fps} fps {height} cm correct exit',lambda: end[0]>(29990 if kind==2 else 30035) and abs(end[2]-(6000+half+(180 if kind==2 else 0)))<6)
            results.append(dict(fps=fps,height=height,kind=kind,end=end,wall_seconds=time.monotonic()-before,observed=observed,serial=start))
            if fps==30:record(str(height),y,False)
    g['report']['traversal_runs']=results
    for height in (60,90,120,180):
        ends=[r['end'] for r in results if r['height']==height]
        check(f'{height} cm endpoints agree across frame caps',lambda e=ends: max(math.dist(e[0],v) for v in e)<3)

    place('A',440,0)
    press('SpaceBar',.035)
    check('121 cm broad ledge selects mantle rather than vault',lambda: player('Server','A')['traversal']==2,3)
    wait(lambda: player('Server','A')['traversal']==0,'121 cm mantle finishes')
    place('A',440,350)
    serial=player('Server','A')['traversal_serial']
    press('SpaceBar')
    time.sleep(.35)
    check('181 cm ledge rejects mantle',lambda: player('Server','A')['traversal']==0 and player('Server','A')['traversal_serial']==serial)

    for label,y in (('too high',1400),('narrow',1750),('low ceiling',-350)):
        place('A',-110,y)
        serial=player('Server','A')['traversal_serial']
        press('SpaceBar')
        time.sleep(.35)
        check(label+' rejects traversal',lambda: player('Server','A')['traversal']==0 and player('Server','A')['traversal_serial']==serial)

    place('B',75,0)
    place('A',-110,0)
    serial=player('Server','A')['traversal_serial']
    press('SpaceBar')
    time.sleep(.4)
    check('Occupied landing rejects traversal',lambda: player('Server','A')['traversal']==0 and player('Server','A')['traversal_serial']==serial)
    place('B',-550,400)
    place('A',-110,0)
    press('SpaceBar',.035)
    check('Repeated-input fixture entered',lambda: player('Server','A')['traversal']==1)
    serial=player('Server','A')['traversal_serial']
    press('SpaceBar',.035)
    check('Repeated Space does not restart traversal',lambda: player('Server','A')['traversal_serial'] in (serial,serial+1))
    wait(lambda: player('Server','A')['traversal']==0,'repeated-input case completes')
    check('Repeated-input case completes exactly once',lambda: player('Server','A')['traversal_serial']==serial+1)
    place('A',-400,2200)
    press('SpaceBar',.035)
    check('Space without obstacle performs normal jump',lambda: player('Server','A')['falling'] and player('Server','A')['traversal']==0)
    wait(lambda: not player('Server','A')['falling'],'normal jump lands')

    place('A',-160,-900)
    record('ladder',-900,True)
    check('Ladder focus',lambda: player('A','A')['ladder_focus']!='None')
    press('E')
    check('Ladder entered on server',lambda: player('Server','A')['traversal']==4)
    check('Authority and both clients evaluate traversal body animation',lambda: all(player(p,'A')['body_montage_active'] for p in peers))
    check('Traversal stows owner and observer weapons',lambda: all(player(p,'A')['weapon_hidden'] for p in ('A','B')) and not player('A','A')['fp_weapon_visible'])
    shots=player('Server','A')['accepted']
    send('A','pulse',action='Fire',duration=.2)
    send('A','inventory',open=True)
    send('A','pulse',action='Reload',duration=.035)
    send('A','medical')
    send('A','ability',kind='heal')
    send('A','swap')
    check('Ladder blocks combat and inventory',lambda: player('Server','A')['accepted']==shots and not player('Server','A')['inventory_open'] and player('Server','A')['combat_state']==0)
    check('Ladder blocks medical and healing channels',lambda: player('Server','A')['medical_end']==0 and player('Server','A')['action']==0)
    press('W',.25)
    z=player('Server','A')['position'][2]
    time.sleep(.5)
    check('Releasing W pauses on ladder',lambda: abs(player('Server','A')['position'][2]-z)<3)
    for peer in ('A','B'):send(peer,'pose',target='EX_A',name='ladder-paused')
    place('B',-150,-805)
    press('E',role='B')
    check('Occupied ladder rejects second player',lambda: player('Server','B')['traversal']==0 and player('Server','A')['traversal']==4)
    place('B',-550,400)
    press('S',.10)
    check('S climbs down',lambda: player('Server','A')['position'][2]<z-8)
    press('SpaceBar')
    check('Space detaches ladder',lambda: player('Server','A')['traversal']==0)
    wait(lambda: not player('Server','A')['falling'],'detach lands')
    place('A',-160,-900)
    press('E');check('Reattach after detach',lambda: player('Server','A')['traversal']==4)
    climb_to_exit('Move')
    check('Ladder exits at top',lambda: player('Server','A')['traversal']==0 and not player('Server','A')['falling'] and player('Server','A')['position'][2]>6300)
    send('A','look',yaw=180)
    press('E');check('Enter ladder from top',lambda: player('Server','A')['traversal']==4)
    climb_to_exit('MoveBack')
    check('Ladder exits at bottom',lambda: player('Server','A')['traversal']==0 and not player('Server','A')['falling'] and player('Server','A')['position'][2]<6150)
    record('ladder',-900,False)
    send('A','look',yaw=0)
    press('E');check('Attach before E release',lambda: player('Server','A')['traversal']==4)
    press('W',.4);press('E')
    check('E releases ladder',lambda: player('Server','A')['traversal']==0)
    wait(lambda: not player('Server','A')['falling'],'E release lands')

    place('A',-160,-900)
    press('E');check('Attach before blocked exit',lambda: player('Server','A')['traversal']==4)
    place('B',85,-900,250)
    press('W',2.2)
    check('Blocked top exit keeps safe ladder attachment',lambda: player('Server','A')['traversal']==4)
    place('B',-550,400)
    press('W',.4)
    check('Cleared exit becomes usable',lambda: player('Server','A')['traversal']==0 and not player('Server','A')['falling'])
    check('Traversal restores owner and observer weapons',lambda: all(not player(p,'A')['weapon_hidden'] for p in ('A','B')) and player('A','A')['fp_weapon_visible'])

    if recording:send('B','camera',target='')
    if g['args'].traversal_lifecycle:
        lifecycle(g,place,press)
    g['report']['scope']='Development geometry and production input; formal route, video and package gates are recorded separately.'


def lifecycle(g,place,press):
    send,check,wait,state,player=(g[k] for k in ('send','check','wait','state','player'))
    place('C',-450,-650)
    place('B',-500,-900)
    place('A',-160,-900)
    press('E');check('Damage case attaches',lambda: player('Server','A')['traversal']==4)
    press('W',.4)
    g['aim']('B','A')
    before=player('Server','A')['health']
    send('B','pulse',action='Fire',duration=.035)
    check('Nonfatal shot damages climbing player without detach',lambda: 0<player('Server','A')['health']<before and player('Server','A')['traversal']==4)
    check('Nonfatal hit preserves traversal body montage',lambda: all(player(p,'A')['body_montage_active'] for p in ('Server','A','B')))
    for _ in range(8):
        send('B','pulse',action='Fire',duration=.035)
        time.sleep(.5)
        if player('Server','A')['life']==5:break
    check('Downing releases traversal',lambda: player('Server','A')['life']==5 and player('Server','A')['traversal']==0)
    place('B',85,-900,250,yaw=180)
    send('B','aim',target='',head=False)
    send('B','look',yaw=180)
    press('E',role='B')
    check('Downed owner releases ladder occupancy',lambda: player('Server','B')['traversal']==4)
    press('SpaceBar',role='B')
    wait(lambda: not player('Server','B')['falling'],'B detaches before rescue')
    place('B',-500,-900)
    place('C',-175,-1020)
    send('C','ability',kind='revive',target='EX_A')
    check('Traversal casualty can be revived normally',lambda: player('Server','A')['life']==0 and player('Server','A')['health']==30,12)
    place('A',-160,-900)
    press('E');check('Revived owner reattaches',lambda: player('Server','A')['traversal']==4)
    press('W',.4)
    g['aim']('B','A')
    for _ in range(10):
        send('B','pulse',action='Fire',duration=.035)
        time.sleep(.5)
        if player('Server','A')['life']==1:break
    check('Fatal damage clears traversal state',lambda: player('Server','A')['life']==1 and player('Server','A')['traversal']==0)
    send('B','aim',target='',head=False)
    place('B',85,-900,250,yaw=180)
    press('E',role='B')
    check('Dead owner releases ladder occupancy',lambda: player('Server','B')['traversal']==4)
    proc=g['processes']['B']
    g['finishing'].add('B')
    g['subprocess'].run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True,check=True)
    proc.wait(timeout=20)
    g['report']['processes']['B'].update(expected_disconnect=True,exit_code=proc.returncode)
    check('Disconnected climber is removed by authority',lambda: not player('Server','B'),30)
    place('C',85,-900,250,yaw=180)
    press('E',role='C')
    check('Disconnect releases ladder for another player',lambda: player('Server','C')['traversal']==4)
    press('SpaceBar',role='C')
    wait(lambda: not player('Server','C')['falling'],'C detaches before extraction')
    centre=state('Server')['exit_position']
    send('Server','fixture',players=[dict(name='EX_C',position=[centre[0],centre[1],centre[2]+100],health=100,shield=0,clear_trauma=True)])
    check('Survivor extracts after traversal lifecycle',lambda: state('C')['account']['outcome']==1,25)
    check('Traversal raid returns to lobby',lambda: state('Server').get('map')=='/Game/Maps/Lobby',40)
