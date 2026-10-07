"""Walk both network peers across resident east geometry, then follow a spectated peer."""
import json
import math
import time


def run(g):
    state,player,send,check,wait=(g[n] for n in ['state','player','send','check','wait'])
    active=g['args'].rendered_role or 'A'
    peer='B' if active=='A' else 'A'
    assert g['ROLES']==['A','B']
    route=json.loads((g['ROOT']/'Scripts/SpaceCenter/east_route.json').read_text(encoding='utf-8'))
    g['report']['east_boundary']='Resident source geometry and collision. Each walker is placed once at the measured old-room start before its first outward walk. All outward, return and spectator movement uses real input; unchanged North streaming manager.'
    g['report']['traversing_role']=active
    snapshots=[]
    loot={box['id']:box for box in state('Server')['loot']}
    inventories={role:player('Server',role)['inventory'] for role in [active,peer]}

    def spectator_follows(east):
        snapshot=state(active)
        target=player(active,peer).get('position')
        authoritative=player('Server',peer)['position']
        view=snapshot['view_position']
        return (snapshot.get('spectated')=='EX_'+peer and target is not None
                and math.dist(target,authoritative)<25 and math.dist(view,target)<500
                and (view[0]>17000 if east else view[0]<0))

    def walk(role,points,label):
        assert not send(role,'walk',points=points)
        distance=sum(math.dist(a,b) for a,b in zip(points,points[1:]))
        deadline=time.monotonic()+max(180,min(600,distance/200+60))
        while time.monotonic()<deadline:
            current=state(role)
            if current.get('error'):raise AssertionError(label+': '+current['error'])
            if current.get('walk_count')==len(points) and current.get('walk_index')==len(points):
                check(label+' grounded',lambda:not player(role,role).get('falling',True),5)
                check(label+' authority agrees',lambda:math.dist(player(role,role)['position'],player('Server',role)['position'])<25)
                snapshots.append({'label':label,'owner':player(role,role),'server':player('Server',role)})
                return
            if g['processes'][role].poll() is not None:raise RuntimeError(role+' exited while walking')
            time.sleep(.15)
        raise TimeoutError(label)

    for role,other in [(active,peer),(peer,active)]:
        # Parking is outside the measured route, behind an existing interior
        # wall. Start each independent walker at the validated route origin.
        parking=[-2100,-300 if other=='B' else -500,460]
        first=route['outward'][0];start=[first[0],first[1],first[2]+94]
        send('Server','fixture',players=[{'name':'EX_'+other,'position':parking,'health':100,'shield':0},
            {'name':'EX_'+role,'position':start,'health':100,'shield':0}])
        check(role+' starts at the measured old-room origin',lambda:math.dist(player('Server',role)['position'][:2],first[:2])<5
              and math.dist(player(role,role)['position'],player('Server',role)['position'])<25
              and not player(role,role).get('falling',True),5)
        check(other+' parking leaves the walking pawn clear',lambda:math.dist(player('Server',role)['position'],player('Server',other)['position'])>150)
        walk(role,route['outward'],role+' east arrival')
        check(role+' reached expanded source area',lambda r=role:player('Server',r)['position'][0]>17000)
        if role==active:send(role,'screenshot',name='east-walk')
        walk(role,route['homeward'],role+' old-area return')
    check('East traversal preserves container identities and contents',lambda:{box['id']:box for box in state('Server')['loot']}==loot)
    check('East traversal preserves both carried inventories',lambda:all(player('Server',r)['inventory']==inventories[r] for r in [active,peer]))
    exit_at=state('Server')['exit_position']
    send('Server','fixture',players=[{'name':'EX_'+active,'position':[exit_at[0],exit_at[1],exit_at[2]+90],'health':100,'shield':0}])
    check('Rendered player extracts normally',lambda:state(active)['account']['outcome']==1,22)
    wait(lambda:state(active).get('spectated')=='EX_'+peer,'Spectator selects living peer')
    walk(peer,route['outward'],'spectated east arrival')
    check('Spectator follows peer into resident east area',lambda:spectator_follows(True))
    send(active,'screenshot',name='east-spectator')
    walk(peer,route['homeward'],'spectated old-area return')
    check('Spectator follows peer back to old interior',lambda:spectator_follows(False))
    g['report']['east_snapshots']=snapshots
    send('Server','end')
    check('East scenario returns every process to lobby',lambda:all(state(r).get('map')=='/Game/Maps/Lobby' for r in g['processes']),40)
