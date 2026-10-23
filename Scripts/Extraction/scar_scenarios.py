"""Real starting SCAR grant, input-driven combat, idempotence and extraction storage.

Position/health are server fixtures; weapons and ammunition come from raid initialization.
"""
import time

def run(g):
    send, check, wait, player = (g[k] for k in ('send','check','wait','player'))
    peers = ['Server']+g['ROLES']
    send('Server','fixture',players=[{'name':'EX_'+r,'position':[-1500-300*i,-300,460],
                                      'health':100,'shield':0} for i,r in enumerate(g['ROLES'])])
    initial=player('Server','A')['inventory']['items']
    scar=next(i for i in initial if i['definition']=='SCARH' and i['slot']==1)
    check('Starting secondary SCAR and 60 reserve',lambda: all(
        any(i['definition']=='SCARH' and i['slot']==1 for i in player(r,r)['inventory']['items'])
        and sum(i['quantity'] for i in player(r,r)['inventory']['items'] if i['definition']=='Ammo76251')==60 for r in g['ROLES']))
    before_retry=player('Server','A')['inventory']
    send('Server','retry_loadout',target='EX_A')
    check('Initialization retry does not regrant supplies',lambda: player('Server','A')['inventory']==before_retry)
    send('A','input_key',key='Two',pressed=True)
    time.sleep(.1)
    send('A','input_key',key='Two',pressed=False)
    wait(lambda: all('SCARH' in player(p,'A').get('weapon','') for p in peers),'SCAR equipped on all peers')
    wait(lambda: player('Server','A')['combat_state']==0,'initial swap completes')
    check('SCAR initial capacity replicated as 20',lambda: all(player(p,'A')['ammo']==20 for p in peers))
    wait(lambda: not player('Server','A')['falling'],'SCAR shooter grounded')
    send('A','aim',target='EX_B',head=False)
    time.sleep(.5)
    send('A','screenshot',name='scar-hip')
    old=player('Server','A')['accepted']
    send('A','pulse',action='Fire',duration=0)
    check('SCAR one authoritative shot',lambda: player('Server','A')['accepted']==old+1)
    check('SCAR spends one round on all peers',lambda: all(player(p,'A')['ammo']==19 for p in peers))
    before={i['definition']:i['quantity'] for i in player('Server','A')['inventory']['items'] if i['definition'] in ['Ammo','Ammo76251']}
    send('A','pulse',action='Reload',duration=0)
    check('SCAR reload restores 20 rounds',lambda: all(player(p,'A')['ammo']==20 for p in peers),15)
    after={i['definition']:i['quantity'] for i in player('Server','A')['inventory']['items'] if i['definition'] in before}
    assert after['Ammo76251']==before['Ammo76251']-1 and after['Ammo']==before['Ammo'],after
    check('SCAR reload consumes only 76251',lambda: True)
    for name in ['AK47','SCARH']:
        send('A','swap')
        check('Weapon swap to '+name,lambda n=name: all(n in player(p,'A').get('weapon','') for p in peers),15)
        wait(lambda: player('Server','A')['combat_state']==0,'swap completes')
    send('A','screenshot',name='scar-after-swap')
    wait(lambda: (g['OUT']/'A-scar-after-swap.png').exists(),'SCAR screenshot written')
    # The current map deliberately retains the labelled temporary exit fixture.
    send('Server','fixture',players=[{'name':'EX_A','position':[1400,2900,460],'health':100,'shield':0}])
    check('Exit starts owner countdown',lambda: player('A','A').get('extract_at',0)>0)
    time.sleep(1)
    send('A','screenshot',name='exit-countdown')
    wait(lambda: (g['OUT']/'A-exit-countdown.png').exists(),'exit HUD screenshot written')
    send('Server','fixture',players=[{'name':'EX_A','position':[-1500,-600,460],'health':100,'shield':0}])
    check('Leaving exit resets owner countdown',lambda: player('A','A').get('extract_at',0)==0)
    send('Server','fixture',players=[{'name':'EX_A','position':[1400,2900,460],'health':100,'shield':0}])
    check('Starting SCAR exits through normal settlement',lambda: g['state']('A').get('account',{}).get('outcome')==1,20)
    account=g['state']('A')['account']
    stored=account['stash']['items']+account['overflow']['items']
    check('Granted SCAR GUID saved in stash',lambda: any(i['id']==scar['id'] for i in stored))
    check('Remaining 76251 reserve saved',lambda: sum(i['quantity'] for i in stored if i['definition']=='Ammo76251')==59)
    g['report']['scope']='Real starting supply, no inventory fixtures; position fixtures, authority combat, retry idempotence, normal extraction storage'
