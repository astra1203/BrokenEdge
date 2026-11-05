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
    if g['args'].soviet_lifecycle:
        return lifecycle(g)
    if g['args'].guards:
        return guard_acceptance(g)
    # Production RPC checks same-world, reach and swing clearance on every authored door.
    doors=sorted(state('Server')['ordinary_doors'],key=lambda d:(d['position'][2],d['name']))
    door=doors[0]
    observer_start=player('Server','B')['position']
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
            if door['name']==doors[0]['name']:
                send('A','input_key',key='F',pressed=True)
                send('A','input_key',key='F',pressed=False)
            else:send('A','ordinary_door',name=door['name'])
            time.sleep(.35)
            if d('Server')['open']:break
        if not d('Server')['open']:
            send('A','screenshot',name='soviet-door-rejected')
            raise AssertionError(('Door request rejected',door['name'],state('A').get('item_message'),state('A').get('door_probe')))
        check('Door opening replicated '+door['name'],lambda:all(d(r)['open'] and abs(d(r)['angle'])>90 for r in peers),5)
        if door['name']==doors[0]['name']:
            current=d('Server')
            place('A',current['approach'],current['yaw'])
            place('B',door['position'])
            send('A','ordinary_door',name=door['name']);time.sleep(.4)
            check('Occupied doorway prevents closing',lambda:all(d(r)['open'] for r in peers)
                  and state('A').get('door_probe',{}).get('reachable')
                  and state('A').get('item_message')=='\u95e8\u8fb9\u6709\u4eba\uff0c\u8bf7\u8ba9\u5f00',2)
            place('B',observer_start)
        current=d('Server')
        for side in (1,-1):
            pos=[current['position'][i]+side*(current['approach'][i]-current['position'][i]) for i in range(3)]
            place('A',pos,current['yaw']+(180 if side<0 else 0))
            send('A','ordinary_door',name=door['name']);time.sleep(.08 if door['name']==doors[0]['name'] else .35)
            if not d('Server')['open']:break
        if door['name']==doors[0]['name']:
            closing=d('Server')
            assert not closing['open'] and 0<abs(closing['angle'])<95, ('Expected moving closing leaf',closing)
            # Saving six full client snapshots can outlast the 0.8 s motion. Arrange
            # the timed obstruction before serializing the assertion evidence.
            send('Server','fixture',players=[dict(name='EX_B',position=door['position'],health=100,shield=0,clear_trauma=True)])
            check('Entering closing swing reverses the door',lambda:all(d(r)['open'] for r in peers),3)
            g['checks'].append(dict(name='Door was moving closed before obstruction',door=closing))
            place('B',observer_start)
            time.sleep(.8)
            current=d('Server')
            place('A',current['approach'],current['yaw'])
            send('A','ordinary_door',name=door['name'])
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
    send('A','screenshot',name='soviet-lobby')
    check('Searched item persisted to stash',lambda:any(i['id']==item['id'] for i in state('A')['account']['stash']['items']))

def continuous_route(g):
    """A bounded collision/navigation route driven solely by ordinary movement input."""
    import json
    send,check,wait,state,player=(g[k] for k in ('send','check','wait','state','player'))
    roles=g['ROLES'];samples=[];legs=[]
    def record():
        (g['OUT']/'ContinuousRoute.json').write_text(json.dumps(dict(teleports=0,legs=legs,samples=samples),indent=2),encoding='utf-8')
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
        legs.append(dict(role=role,label=label,points=points))
        record()
        send(role,'walk',points=dense)
        last=[0]
        def completed():
            s=state(role);p=player('Server',role)
            assert not s.get('error'),s.get('error')
            assert p.get('life')==0, ('Player lost during route',label,p)
            if time.monotonic()-last[0]>.5:
                last[0]=time.monotonic()
                samples.append(dict(role=role,leg=label,time=state('Server').get('world_time'),position=p.get('position'),falling=p.get('falling')))
                record()
            return s.get('walk_count')==len(dense) and s.get('walk_index')==len(dense)
        try:check('Walk completed: '+label,completed,180)
        except Exception:
            send(role,'screenshot',name='soviet-route-stalled')
            raise
        legs[-1]['actual_end']=player('Server',role)['position']
        record()
    box=next(x for x in state('Server')['loot'] if x.get('source_point')=='SovietBox00')
    # Runtime containers carve navigation; query a reachable interaction position,
    # never a path whose endpoint is inside the solid box.
    approaches=[]
    for angle in range(0,360,45):
        a=math.radians(angle);p=box['position']
        send('Server','path_query',start=player('Server','A')['position'],end=[p[0]+150*math.cos(a),p[1]+150*math.sin(a),p[2]])
        q=state('Server')['path_probe']
        if q['valid']:approaches.append(q['points'])
    assert approaches, 'No reachable interaction position around authored supplies'
    points=min(approaches,key=lambda ps:sum(math.dist(a,b) for a,b in zip(ps,ps[1:])))
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
    check('Walking route returns all players to lobby',lambda:all(state(r).get('map')=='/Game/Maps/Lobby' for r in ['Server']+roles),40)

def guard_acceptance(g):
    """Production AI and gunfire, with positions arranged by an explicit scene fixture."""
    send,check,wait,state,player=(g[k] for k in ('send','check','wait','state','player'))
    check('All military patrol segments have runtime navigation',lambda:len(state('Server')['nav_paths'])==18 and all(p['valid'] for p in state('Server')['nav_paths']))
    initial={p['name']:p['position'] for p in state('Server')['guards']}
    check('Authored guards actually patrol',lambda:sum(math.dist(p['position'],initial[p['name']])>100 for p in state('Server')['guards'])>=4,25)
    target=min(state('Server')['guards'],key=lambda p:p['position'][1])['name']
    def guard():return next((p for p in state('Server')['guards'] if p['name']==target),{})
    safe=player('Server','A')['position']
    def place(role,p):
        send('Server','fixture',players=[dict(name='EX_'+role,position=p,health=100,shield=0,clear_trauma=True)])
        time.sleep(.5)
    def approach():
        for angle in (0,90,180,270,45,135,225,315):
            p=guard()['position'];a=math.radians(angle)
            candidate=[p[0]+500*math.cos(a),p[1]+500*math.sin(a),p[2]]
            send('Server','path_query',start=p,end=candidate)
            q=state('Server')['path_probe']
            if not q['valid']:continue
            floor=q['points'][-1];place('A',[floor[0],floor[1],floor[2]+95])
            if guard().get('line_of_sight_to_A'):return
        raise AssertionError('No accessible line of sight to authored guard')
    approach()
    original=guard();gun=next(i for i in original['inventory']['items'] if i['slot']==0)
    send('A','aim',target=target,head=False)
    for _ in range(5):
        send('A','pulse',action='Fire');time.sleep(.3)
        if guard().get('health',100)<100:break
    check('Production bullets damage the authored guard',lambda:0<guard().get('health',100)<100)
    check('Authored guard acquires attacking player',lambda:guard().get('target')==player('Server','A').get('pawn_name'),8)
    check('Authored guard returns fire',lambda:guard().get('shots',0)>original.get('shots',0),8)
    send('A','screenshot',name='soviet-guard-combat')
    place('A',safe)
    check('Guard loses distant target and returns to patrol',lambda:guard().get('state')==0 and guard().get('target')=='None',35)
    approach();send('A','aim',target=target,head=True)
    for _ in range(20):
        send('A','pulse',action='Fire');time.sleep(.25)
        if guard().get('dead') or not guard():break
    check('Guard defeat credited through combat',lambda:player('Server','A').get('guard_kills',0)>=1,5)
    check('Guard weapon appears in one death container',lambda:sum(any(i['id']==gun['id'] for i in box.get('contents',{}).get('items',[])) for box in state('Server')['loot'])==1)
    send('A','aim',target='',head=False)
    zone=state('Server')['exits'][0]['position']
    for i,r in enumerate(g['ROLES']):place(r,[zone[0]+(i%3-1)*150,zone[1]+(i//3)*150,zone[2]+100])
    check('Guard encounter ends with durable extraction',lambda:all(state(r).get('account',{}).get('outcome')==1 for r in g['ROLES']),35)

def lifecycle(g):
    """Keep death/disconnect/timeout inventory paths separate from successful extraction."""
    import subprocess, json
    send,check,state,player=(g[k] for k in ('send','check','state','player'))
    # ExtractionItems::Protected retains SafeSlot (6) and SafeGrid (-3).
    possessions={r:{i['id']:i for i in player('Server',r)['inventory']['items'] if i['slot'] not in (6,-3)} for r in ('B','C')}
    protected={r:{i['id'] for i in player('Server',r)['inventory']['items'] if i['slot'] in (6,-3)} for r in ('A','B','C')}
    carried={i['id'] for i in player('Server','A')['inventory']['items'] if i['slot'] not in (6,-3)}
    disconnected_profile=state('C')['account']['profile']
    g['finishing'].add('C')
    g['report']['processes']['C']['expected_disconnect']=True
    subprocess.run(['taskkill','/PID',str(g['processes']['C'].pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.STDOUT)
    check('Military abrupt disconnect leaves two survivors',lambda:state('Server').get('alive')==2,30)
    check('Abrupt disconnect is committed to authoritative storage',lambda:any(p['profile']==disconnected_profile and p['outcome']==4 for p in state('Server')['stored_results']))
    def corpse(role):
        return [box for box in state('Server')['loot'] if {i['id'] for i in box['contents']['items']}==set(possessions[role])]
    check('Disconnected possessions move to exactly one corpse',lambda:len(corpse('C'))==1)
    start=player('Server','A')['position']
    # A fixed +X offset can put the victim behind a rock at a randomly selected spawn.
    # Keep the death test separate from cover penetration: verify both camera and barrel rays.
    attempts=[];clear=False
    send('Server','fixture',players=[dict(name='EX_A',position=start,health=100,shield=0,clear_trauma=True)])
    for angle in range(0,360,45):
        a=math.radians(angle)
        candidate=[start[0]+400*math.cos(a),start[1]+400*math.sin(a),start[2]]
        send('Server','path_query',start=start,end=candidate)
        q=state('Server')['path_probe']
        if not q['valid']:
            attempts.append(dict(candidate=candidate,navigable=False));continue
        floor=q['points'][-1]
        send('Server','fixture',players=[dict(name='EX_B',position=[floor[0],floor[1],floor[2]+95],health=10,shield=0,clear_trauma=True)])
        time.sleep(.6)
        send('A','aim',target='EX_B',head=False)
        time.sleep(.4)
        probe=state('A').get('aim_diagnostics',{})
        target=probe.get('target_actor')
        selected=probe.get('points',{}).get('selected',{})
        rays=[selected.get(k,{}) for k in ('camera_simple','camera_complex','muzzle_complex_sphere')]
        clear=bool(target and probe.get('muzzle_probe_available') and all(
            'blocking' in h and (not h['blocking'] or h.get('actor')==target) for h in rays))
        attempts.append(dict(candidate=candidate,navigable=True,clear=clear,probe=probe))
        if clear:break
    (g['OUT']/'LifecycleFixture.json').write_text(json.dumps(attempts,indent=2)+'\n',encoding='utf-8')
    check('Lifecycle PVP fixture has clear camera and muzzle rays',lambda:clear)
    send('A','screenshot',name='soviet-lifecycle-clear-shot')
    for _ in range(12):
        send('A','pulse',action='Fire');time.sleep(.3)
        if player('Server','B').get('life')==1:break
    check('Military PVP death settles durably',lambda:state('B').get('account',{}).get('outcome')==2)
    check('Dead player possessions move to exactly one corpse',lambda:len(corpse('B'))==1)
    for role in ('B','C'):
        check(role+' corpse preserves quantities and magazines',lambda r=role:all(i['quantity']==possessions[r][i['id']]['quantity'] and i['magazine']==possessions[r][i['id']]['magazine'] for i in corpse(r)[0]['contents']['items']))
        check(role+' protected inventory never drops into world',lambda r=role:not protected[r].intersection(i['id'] for box in state('Server')['loot'] for i in box['contents']['items']))
    send('A','aim',target='',head=False)
    check('Real military round deadline settles survivor as timed out',lambda:state('A').get('account',{}).get('outcome')==5,130)
    check('Military timeout returns connected players to lobby',lambda:all(state(r).get('map')=='/Game/Maps/Lobby' for r in ('Server','A','B')),40)
    check('Timed out carried loot is not duplicated into stash',lambda:not carried.intersection(i['id'] for i in state('A')['account']['stash']['items']))
    for role in ('A','B'):
        check(role+' safe equipment persists after failed raid',lambda r=role:protected[r].issubset(i['id'] for container in ('loadout','stash') for i in state(r)['account'][container]['items']))
    check('Military lobby readiness resets after failed raid',lambda:all(not p.get('ready') for p in state('Server')['players']))
