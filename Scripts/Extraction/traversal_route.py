"""Continuous actual-input route on the shipped source map; one initial placement."""
import hashlib
import json
import math
import time
from pathlib import Path

def run(g):
    send,check,wait,state,player=(g[k] for k in ('send','check','wait','state','player'))
    root,out=g['ROOT'],g['OUT']
    routefile=root/'Scripts/Traversal/formal_route.json'
    route=json.loads(routefile.read_text(encoding='utf-8'))
    g['report']['input_sha256'][routefile.relative_to(root).as_posix()]=hashlib.sha256(routefile.read_bytes()).hexdigest()
    g['report']['input_sha256']['Scripts/Extraction/traversal_route.py']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    old=json.loads((root/'Scripts/SpaceCenter/route.json').read_text(encoding='utf-8'))
    segments={s['name']:s['points'] for s in old['segments']}
    half=player('Server','A')['capsule_half']
    start=segments['stairs_up'][0]
    send('Server','fixture',players=[dict(name='EX_A',position=[start[0],start[1],start[2]+half+3],health=100,shield=0,clear_trauma=True),
                                   dict(name='EX_B',position=[-2100,-300,460],health=100,shield=0,clear_trauma=True)])
    wait(lambda: math.dist(player('A','A')['position'][:2],start[:2])<5,'formal route placement')
    send('A','fps',value=60)
    recording=g['args'].traversal_record
    def record(name,point,enabled):
        if not recording:return
        if enabled:
            position=[point[0]-480,point[1]-380,point[2]+320]
            target=[point[0],point[1],point[2]+110]
            if name=='mantle':
                position=[point[0]+250,point[1]-400,point[2]+320]
                target=[point[0],point[1]+40,point[2]+190]
            elif name=='ladder':
                target=[point[0]+45,point[1]+75,point[2]+230]
            send('B','world_camera',position=position,target=target,field_of_view=80 if name=='ladder' else 70)
        for peer in ('A','B'):send(peer,'record',enabled=enabled,name='formal-'+name)
    def refine(target):
        # The general walking harness stops within 45 cm. Approach the measured opening
        # with short real input pulses; never replace precision movement with a teleport.
        for _ in range(100):
            p=player('A','A')['position']
            delta=[target[0]-p[0],target[1]-p[1]]
            if math.hypot(*delta)<2:break
            send('A','look',yaw=math.degrees(math.atan2(delta[1],delta[0])))
            send('A','pulse',action='Move',duration=0)
            time.sleep(.06)
        check('Formal precise approach converges',lambda: math.dist(player('Server','A')['position'][:2],target[:2])<4,5)
    def walk(points,label):
        send('A','walk',points=points)
        deadline=time.monotonic()+max(60,sum(math.dist(a,b) for a,b in zip(points,points[1:]))/150+30)
        while time.monotonic()<deadline:
            s=state('A')
            if s.get('error'):raise AssertionError(label+': '+s['error'])
            if s.get('walk_count')==len(points) and s.get('walk_index')==len(points):break
            time.sleep(.1)
        else:raise TimeoutError(label)
        check('Formal route '+label,lambda: not player('Server','A')['falling'] and math.dist(player('Server','A')['position'][:2],points[-1][:2])<55,5)
    walk(segments['stairs_up'],'ordinary stairs up')
    check('Stairs reach upper floor using walking',lambda: abs(player('Server','A')['position'][2]-(747.21+half))<8 and player('Server','A')['traversal']==0)
    walk(segments['stairs_down'],'ordinary stairs down')
    walk(route['to_vault'],'walk to source barrier')
    for name,kind in (('vault',1),('mantle',2)):
        if name=='mantle':walk(route['to_mantle'],'walk to source platform')
        target=route[name]
        refine(target['start'])
        if g['args'].debug_hold and g['args'].traversal_debug_point=='vault' and name=='vault':wait(lambda: (out/'Continue.txt').exists(),'formal traversal debugger prepared',900)
        send('A','look',yaw=target['yaw'])
        send('A','screenshot',name='formal-'+name+'-approach')
        record(name,target['start'],True)
        send('A','pulse',action='Jump',duration=.035)
        check('Formal source '+name+' starts',lambda: player('Server','A')['traversal']==kind,4)
        if not recording:send('A','screenshot',name='formal-'+name)
        check('Formal source '+name+' lands',lambda: player('Server','A')['traversal']==0 and not player('Server','A')['falling'] and abs(player('Server','A')['position'][2]-(target['end'][2]+half))<8,5)
        record(name,target['start'],False)
    walk(route['to_ladder'],'walk off platform to source ladder')
    send('A','look',yaw=route['ladder']['yaw'])
    check('Formal source ladder is interactable',lambda: player('A','A')['ladder_focus']!='None')
    record('ladder',route['ladder']['bottom'],True)
    send('A','pulse',action='Equip',duration=.05)
    check('Formal source ladder attaches',lambda: player('Server','A')['traversal']==4)
    for peer in ('A','B'):send(peer,'pose',target='EX_A',name='formal-ladder-attached')
    if g['args'].debug_hold and g['args'].traversal_debug_point=='ladder':wait(lambda: (out/'Continue.txt').exists(),'source ladder debugger prepared',900)
    send('A','pulse',action='Move',duration=4)
    wait(lambda: player('Server','A')['traversal'] in (5,0),'source ladder reaches roof transition',5)
    send('A','pulse',action='Move',duration=0)
    end=route['ladder']['top_exit']
    check('Formal source ladder reaches roof',lambda: player('Server','A')['traversal']==0 and not player('Server','A')['falling'] and math.dist(player('Server','A')['position'],[end[0],end[1],end[2]+half])<15,8)
    record('ladder',route['ladder']['bottom'],False)
    send('A','screenshot',name='formal-ladder-top')
    g['report']['formal_route']=dict(result='passed',ladder=route['ladder']['source_id'],initial_placements=1,teleports_between_segments=0)
