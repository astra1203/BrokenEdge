"""Server-selected source safes: actual production search sessions, no spawned loot fixtures."""
import json
import math
import os
import time

def run(g):
    send,check,wait,state,player=(g[k] for k in ('send','check','wait','state','player'))
    audit=json.loads((g['ROOT']/'Artifacts/Extraction/RestorationEvidence/Loot/PlacementAudit.json').read_text(encoding='utf-8'))
    points={p['id']:p for p in audit['points'] if p['variant']=='SpaceCenter_Ma1' and p['enabled']}
    def safes(peer='Server'):
        return {a['source_point']:a for a in state(peer)['loot'] if a.get('source_point') in points}
    selected=safes()
    groups={p['group'] for p in points.values() if p['group']!='None'}
    for group in groups:
        required=3 if group.endswith('Three') else 2
        check('All authored candidates enabled '+group,lambda group=group,required=required:sum(p['group']==group for p in points.values())==required)
        check('Exactly one safe from '+group,lambda group=group:sum(points[k]['group']==group for k in safes())==1)
    expected=sum(p['group']=='None' for p in points.values())+len(groups)
    check('Fixed and grouped safe count',lambda:len(safes())==expected)
    original={k:(a['id'],a['contents']) for k,a in selected.items()}
    for index,(point,box) in enumerate(selected.items()):
        p=box['position'];opened=False
        for angle in [0,90,180,270]:
            x=math.cos(math.radians(angle))*100;y=math.sin(math.radians(angle))*100
            send('Server','fixture',players=[dict(name='EX_A',position=[p[0]+x,p[1]+y,p[2]+100],health=100,shield=0),
                                           dict(name='EX_B',position=[p[0]+x*1.4,p[1]+y*1.4,p[2]+100],health=100,shield=0)])
            wait(lambda:point in safes('A') and point in safes('B'),'safe replicates near both clients')
            send('A','look',yaw=angle+180,pitch=-40)
            time.sleep(.6)
            send('A','screenshot',name='safe-world-%02d-%d'%(index,angle))
            send('A','open_loot',target=box['id'])
            time.sleep(.8)
            if player('A','A').get('loot_id')==box['id']:
                opened=True;break
        check('Authored safe searchable '+point,lambda:opened)
        send('A','screenshot',name='safe-%02d'%index)
        send('A','inventory',open=False)
        time.sleep(.4)
        wait(lambda:not player('A','A').get('inventory_open'),'close safe')
        send('A','open_loot',target=box['id'])
        check('Reopening preserves safe identity '+point,lambda:player('A','A').get('loot_id')==box['id'])
        send('A','inventory',open=False)
    check('Search and travel never reroll or refill safes',lambda:original=={k:(a['id'],a['contents']) for k,a in safes().items()})
    # Race for one real authored safe item after both independent searches finish.
    both_open=False
    for angle in [0,90,180,270]:
        radians=math.radians(angle);fixtures=[]
        for role,side in [('A',-65),('B',65)]:
            fixtures.append(dict(name='EX_'+role,position=[p[0]+130*math.cos(radians)-side*math.sin(radians),
                p[1]+130*math.sin(radians)+side*math.cos(radians),p[2]+100],health=100,shield=0))
        send('Server','fixture',players=fixtures);time.sleep(1)
        for role in ['A','B']:send(role,'open_loot',target=box['id'])
        time.sleep(1)
        if all(player(r,r).get('loot_id')==box['id'] for r in ['A','B']):both_open=True;break
        for role in ['A','B']:send(role,'inventory',open=False)
    check('Two unobstructed viewers can search authored safe',lambda:both_open)
    for role in ['A','B']:
        wait(lambda role=role:all(i['definition']!='None' for i in player(role,role)['loot_view']['items']),'independent safe search completes')
    before=safes()[point]['contents'];target=before['items'][0]['id']
    sequences={r:state(r).get('item_sequence',0) for r in ['A','B']}
    for role in ['A','B']:
        ident=g['commands'].get(role,0)+1;g['commands'][role]=ident
        path=g['OUT']/(role+'-command.json');temporary=path.with_suffix('.race.tmp')
        temporary.write_text(json.dumps({'id':ident,'op':'item','item':target,'quantity':before['items'][0]['quantity'],
            'from_loot':True,'loot_revision':before['revision']}),encoding='utf-8')
        for attempt in range(100):
            try:os.replace(temporary,path);break
            except PermissionError:
                if attempt==99:raise
                time.sleep(.01)
    wait(lambda:all(state(r).get('item_sequence',0)>sequences[r] for r in sequences),'both race responses')
    check('Authored safe concurrent take has exactly one owner',lambda:
        sum(i['id']==target for p in state('Server')['players'] for i in p.get('inventory',{}).get('items',[]))==1
        and all(i['id']!=target for i in safes()[point]['contents']['items']))
    for role in ['A','B']:send(role,'inventory',open=False)
    send('A','open_loot',target=box['id'])
    check('Reopen after taking does not replenish safe',lambda:all(i['id']!=target for i in safes()[point]['contents']['items']))
    # Active rounds intentionally reject late joins; do not bypass that production policy.
    final={k:(a['id'],a['contents']) for k,a in safes().items()}
    g['launch']('C')
    send('C','direct_join',expect_rejection=True)
    check('Late join obeys active-round rejection policy',lambda:'MATCH_IN_PROGRESS' in (g['OUT']/'C.log').read_text(encoding='utf-8',errors='replace'),35)
    check('Rejected late join cannot reroll or replenish authored safes',lambda:final=={k:(a['id'],a['contents']) for k,a in safes().items()})
