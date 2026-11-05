"""Military base acceptance, using production RPCs in editor or packaged processes."""
import math, time

def run(g):
    send,check,wait,state,player=(g[k] for k in ('send','check','wait','state','player'))
    roles=g['ROLES'];peers=['Server']+roles
    check('All peers use military base configuration',lambda:all(state(r).get('map_id')=='SovietMilitary' for r in peers))
    check('Three authored extraction zones',lambda:all(len(state(r).get('exits',[]))==3 for r in peers))
    check('24 authored containers',lambda:sum(x.get('source_point','').startswith('SovietBox') for x in state('Server')['loot'])==24)
    check('Ten scattered pickups',lambda:sum(x['loose'] for x in state('Server')['loot'])==10)
    check('17 replicated ordinary doors',lambda:all(len(state(r).get('ordinary_doors',[]))==17 for r in peers))
    check('Spawns have separate collision capsules',lambda:all(math.dist(player('Server',a)['position'],player('Server',b)['position'])>84 for i,a in enumerate(roles) for b in roles[i+1:]))
    if g['args'].guards:
        check('Configured guards spawned',lambda:len(state('Server').get('guards',[]))==g['args'].guards)
    if g['args'].soviet_route:
        return continuous_route(g)
    # Production RPC checks same-world, reach and swing clearance on every authored door.
    doors=sorted(state('Server')['ordinary_doors'],key=lambda d:(d['position'][2],d['name']))
    door=doors[0]
    def d(peer): return next(x for x in state(peer)['ordinary_doors'] if x['name']==door['name'])
    def place(role,p,yaw=0):
        send('Server','fixture',players=[dict(name='EX_'+role,position=p,health=100,shield=0,yaw=yaw,clear_trauma=True)])
        time.sleep(.6)
        send(role,'look',yaw=yaw)
    send('B','ordinary_door',name=door['name']);time.sleep(.4)
    check('Distant client cannot toggle door',lambda:not d('Server')['open'])
    for door in doors if not g['args'].guards else doors[:1]:
        for side in (1,-1):
            pos=[door['position'][i]+side*(door['approach'][i]-door['position'][i]) for i in range(3)]
            place('A',pos,door['yaw']+(180 if side<0 else 0))
            send('A','ordinary_door',name=door['name']);time.sleep(.35)
            if d('Server')['open']:break
        if not d('Server')['open']:
            send('A','screenshot',name='soviet-door-rejected')
            raise AssertionError(('Door request rejected',door['name'],state('A').get('item_message'),state('A').get('door_probe')))
        check('Door opening replicated '+door['name'],lambda:all(d(r)['open'] and abs(d(r)['angle'])>90 for r in peers),5)
        current=d('Server')
        for side in (1,-1):
            pos=[current['position'][i]+side*(current['approach'][i]-current['position'][i]) for i in range(3)]
            place('A',pos,current['yaw']+(180 if side<0 else 0))
            send('A','ordinary_door',name=door['name']);time.sleep(.35)
            if not d('Server')['open']:break
        check('Door closing replicated '+door['name'],lambda:all(not d(r)['open'] and abs(d(r)['angle'])<1 for r in peers),5)
    send('A','screenshot',name='soviet-door')
    if not g['args'].guards:
        for candidate in [x for x in state('Server')['loot'] if x.get('source_point','').startswith('SovietBox')]:
            reached=False;p=candidate['position']
            for angle in (0,90,180,270):
                rad=math.radians(angle)
                place('A',[p[0]+130*math.cos(rad),p[1]+130*math.sin(rad),p[2]+98],angle+180)
                send('A','open_loot',target=candidate['id']);time.sleep(.3)
                reached=player('A','A').get('loot_id')==candidate['id']
                send('A','inventory',open=False)
                if reached:break
            check('Container interaction reachable '+candidate['source_point'],lambda:reached,1)
    # Search a real authored container, trying each side to avoid placement-dependent obstructions.
    box=next(x for x in state('Server')['loot'] if x.get('source_point')=='SovietBox00')
    p=box['position'];opened=False
    for angle in (0,90,180,270):
        rad=math.radians(angle)
        place('A',[p[0]+130*math.cos(rad),p[1]+130*math.sin(rad),p[2]+98],angle+180)
        send('A','open_loot',target=box['id']);time.sleep(.7)
        if player('A','A').get('loot_id')==box['id']: opened=True;break
        send('A','inventory',open=False)
    check('Authored container reachable through real search request',lambda:opened)
    wait(lambda:all(i['definition']!='None' for i in player('A','A')['loot_view']['items']),'Search reveals authored contents')
    send('A','screenshot',name='soviet-search')
    item=next(i for i in player('A','A')['loot_view']['items'] if i['definition']=='Bandage')
    send('A','item',item=item['id'],from_loot=True,quantity=item['quantity'],auto=True)
    check('Authored loot transferred into inventory',lambda:any(i['id']==item['id'] for i in player('Server','A')['inventory']['items']))
    send('A','inventory',open=False)
    send('A','input_key',key='M',pressed=True)
    send('A','input_key',key='M',pressed=False)
    check('Tactical map opens',lambda:'ExtractionTacticalMap' in state('A').get('visible_widgets',[]))
    send('A','screenshot',name='soviet-map')
    send('A','input_key',key='Escape',pressed=True)
    send('A','input_key',key='Escape',pressed=False)
    # Enter an actual map exit; never spawn a test exit or directly alter settlement.
    zone=state('Server')['exits'][0]['position']
    for i,r in enumerate(roles):place(r,[zone[0]+(i%3-1)*150,zone[1]+(i//3)*150,zone[2]+100])
    check('Individual exit timers started',lambda:all(player('Server',r).get('extract_at',0)>0 for r in roles))
    send('A','screenshot',name='soviet-extraction')
    check('All players durably extracted',lambda:all(state(r).get('account',{}).get('outcome')==1 for r in roles),35)
    check('Returns to lobby after settlement',lambda:all(state(r).get('map')=='/Game/Maps/Lobby' for r in peers),40)
    check('Searched item persisted to stash',lambda:any(i['id']==item['id'] for i in state('A')['account']['stash']['items']))

def continuous_route(g):
    """A bounded collision/navigation route driven solely by ordinary movement input."""
    import json
    send,check,wait,state,player=(g[k] for k in ('send','check','wait','state','player'))
    roles=g['ROLES'];samples=[];legs=[]
    def path(role,end):
        send('Server','path_query',start=player('Server',role)['position'],end=end)
        q=state('Server')['path_probe']
        assert q['valid'], ('No complete runtime path',role,end)
        return q['points']
    def walk(role,points,label):
        # Short segments avoid cutting across corners at the driver's arrival radius.
        dense=[]
        for a,b in zip(points,points[1:]):
            count=max(1,math.ceil(math.dist(a,b)/120))
            dense.extend([[a[k]+(b[k]-a[k])*i/count for k in range(3)] for i in range(1,count+1)])
        assert dense
        send(role,'walk',points=dense)
        last=[0]
        def completed():
            s=state(role);p=player('Server',role)
            assert not s.get('error'),s.get('error')
            assert p.get('life')==0, ('Player lost during route',label,p)
            if time.monotonic()-last[0]>.5:
                last[0]=time.monotonic()
                samples.append(dict(role=role,leg=label,time=state('Server').get('world_time'),position=p.get('position'),falling=p.get('falling')))
            return s.get('walk_count')==len(dense) and s.get('walk_index')==len(dense)
        check('Walk completed: '+label,completed,180)
        legs.append(dict(role=role,label=label,points=points,actual_end=player('Server',role)['position']))
        (g['OUT']/'ContinuousRoute.json').write_text(json.dumps(dict(teleports=0,legs=legs,samples=samples),indent=2),encoding='utf-8')
    box=next(x for x in state('Server')['loot'] if x.get('source_point')=='SovietBox00')
    points=path('A',box['position'])
    # Stop 110 cm before the box, within the normal search range.
    a,b=points[-2:];length=math.dist(a[:2],b[:2]);assert length>110
    points[-1]=[b[k]+(a[k]-b[k])*110/length for k in range(3)]
    walk('A',points,'spawn to authored supplies')
    send('A','open_loot',target=box['id'])
    check('Walking route reaches searchable container',lambda:player('A','A').get('loot_id')==box['id'])
    wait(lambda:bool(player('A','A')['loot_view']['items']) and all(i['definition']!='None' for i in player('A','A')['loot_view']['items']),'Search completes')
    item=next(i for i in player('A','A')['loot_view']['items'] if i['definition']=='Bandage')
    send('A','item',item=item['id'],from_loot=True,quantity=item['quantity'],auto=True)
    check('Walking route loot is in authoritative inventory',lambda:any(i['id']==item['id'] for i in player('Server','A')['inventory']['items']))
    send('A','screenshot',name='soviet-route-search')
    send('A','inventory',open=False)
    zone=state('Server')['exits'][0]['position']
    for role in roles:
        walk(role,path(role,zone),'walk to authored exit '+role)
        check(role+' real extraction timer starts',lambda r=role:player('Server',r).get('extract_at',0)>0,5)
    check('Walking route persists extraction for all players',lambda:all(state(r).get('account',{}).get('outcome')==1 for r in roles),40)
    check('Walking route returns searched item to stash',lambda:any(i['id']==item['id'] for i in state('A')['account']['stash']['items']))
