"""Presentation and actual lobby -> raid -> extraction -> lobby flow, isolated accounts."""
import time

def run(g):
    send,state,player,wait,check=(g[k] for k in ['send','state','player','wait','check'])
    roles=g['ROLES']
    def visible(name):return name in state('A').get('visible_widgets',[])
    for role in roles:wait(lambda role=role:state(role).get('account',{}).get('bound'),'account bound')
    before={r:state(r)['account']['stash'] for r in roles}
    if len(roles)>1:
        send('B','squad',value=1)
        check('Lobby squad switch replicates',lambda:player('Server','B').get('squad')==1)
    time.sleep(3)
    check('Each client owns one local showcase and server owns none',lambda:
        state('Server').get('lobby_showcases')==0 and all(state(r).get('lobby_showcases')==1 for r in roles))
    send('A','screenshot',name='lobby')
    check('Preview never changes stored inventory',lambda:all(state(r)['account']['stash']==before[r] for r in roles))
    for button,widget,label in [('StashButton','ExtractionStashWidget','stash'),
                                ('ShopButton','ExtractionProgressionWidget','shop'),
                                ('QuestsButton','ExtractionProgressionWidget','quests')]:
        send('A','lobby_button',name=button)
        check(label+' opens from lobby',lambda widget=widget:visible(widget))
        time.sleep(1);send('A','screenshot',name=label)
        if label=='stash':
            item=next(i for i in state('A')['account']['stash']['items'] if i['definition']=='AK')
            # Grab the item centre in both directions. Equipped cards use a
            # centre anchor; the old destination is now a one-row pocket.
            source=(105+(item['x']+item['width']/2)*38,165+(item['y']+item['height']/2)*38)
            destination=(595,185)  # Current primary weapon slot, 520/165 + 150/40.
            for start,end,target in [(source,destination,'loadout'),(destination,source,'stash')]:
                revision=state('A')['account']['revision']
                send('A','mouse',x=start[0],y=start[1],action='down')
                send('A','mouse',x=end[0],y=end[1],action='move')
                send('A','mouse',x=end[0],y=end[1],action='up')
                wait(lambda:state('A')['account']['revision']>revision,'real stash drag commits')
                check('Real stash drag transfers the same item to '+target,lambda target=target:
                    any(i['id']==item['id'] and (i['slot']==0 if target=='loadout' else
                        i['x']==item['x'] and i['y']==item['y']) for i in state('A')['account'][target]['items']))
            check('Scaled stash drag preserves item identity',lambda:any(i['id']==item['id'] for i in state('A')['account']['stash']['items']))
        send('A','key',key='Escape')
        check(label+' closes and lobby remains',lambda widget=widget:not visible(widget) and visible('LobbyWidget'))
    if len(roles)>1:
        send('A','ready');check('Ready state replicates',lambda:player('Server','A').get('ready'))
        send('A','ready');check('Ready cancellation replicates',lambda:not player('Server','A').get('ready'))
    primary=next(i for i in state('A')['account']['stash']['items'] if i['definition']=='AK')
    revision=state('A')['account']['revision']
    send('A','storage',item=primary['id'],quantity=1,slot=0)
    wait(lambda:state('A')['account']['revision']>revision,'equip primary in lobby')
    check('Preview follows committed primary weapon',lambda:state('A').get('lobby_weapon')=='AK')
    time.sleep(1);send('A','screenshot',name='lobby-equipped')
    revision=state('A')['account']['revision']
    send('A','storage',item=primary['id'],quantity=1,**{'from':1,'to':0})
    wait(lambda:state('A')['account']['revision']>revision,'return primary to stash')
    check('Unequip clears presentation without losing inventory',lambda:state('A').get('lobby_weapon')=='None' and any(i['id']==primary['id'] for i in state('A')['account']['stash']['items']))
    carried_rifle=next(i['id'] for i in state('A')['account']['stash']['items'] if i['definition']=='AK')
    g['begin_round']()
    check('Lobby rifle identity survives starting-supply relocation',lambda:
        any(i['id']==carried_rifle and i['quantity']==1 for i in player('Server','A')['inventory']['items']))
    check('Travel destroys presentation actors',lambda:all(state(r).get('lobby_showcases')==0 for r in roles))
    send('A','screenshot',name='raid-hud')
    send('A','input_key',key='M',pressed=True);send('A','input_key',key='M',pressed=False)
    check('Tactical map opens through input',lambda:visible('ExtractionTacticalMap'))
    time.sleep(1);send('A','screenshot',name='tactical-map')
    send('A','input_key',key='Escape',pressed=True);send('A','input_key',key='Escape',pressed=False)
    check('Tactical map closes through input',lambda:not visible('ExtractionTacticalMap'))
    send('A','inventory',open=True);time.sleep(1);send('A','screenshot',name='raid-inventory')
    send('A','key',key='Escape')
    check('Raid inventory closes through keyboard',lambda:not player('A','A').get('inventory_open'))
    # This scenario validates UI/travel/settlement, with explicit placement and
    # opening-time fixtures. Keep the authored hold timer and record overrides.
    selected=next(e for e in state('Server')['exits'] if e['id']=='OfficialExitTestrange')
    overrides=dict(opens_after=0,closes_after=0,capacity=max(selected['capacity'],len(roles)),require_empty_pack=False)
    g['report']['lobby_ui_exit_fixture']=dict(original=selected,overrides=overrides,
        scope='teleported settlement; not route, opening-time, empty-backpack or capacity acceptance')
    error=send('Server','exit_config',exit_id=selected['id'],**overrides)
    assert not error,error
    check('Recorded exit opening fixture replicates',lambda:all(any(e['id']==selected['id'] and e['available']
        for e in state(r).get('exits',[])) for r in ['Server',*roles]))
    centre=selected['bounds_center']
    send('Server','fixture',players=[dict(name='EX_'+r,position=[centre[0]+(i%3-1)*130,centre[1]+(i//3)*130,centre[2]],health=100,shield=0) for i,r in enumerate(roles)])
    check('All players extract through production hold timer',lambda:all(state(r).get('account',{}).get('outcome')==1 for r in roles),max(30,selected['hold_seconds']+15))
    send('A','screenshot',name='settlement')
    check('Returns to lobby with unlocked accounts',lambda:all(state(r).get('map')=='/Game/Maps/Lobby' and state(r).get('account',{}).get('bound') and not state(r)['account'].get('locked') for r in roles),50)
    check('Extraction returns the carried rifle once',lambda:
        sum(i['quantity'] for i in state('A')['account']['stash']['items'] if i['id']==carried_rifle)==1)
    check('Return recreates exactly one local showcase',lambda:all(state(r).get('lobby_showcases')==1 for r in roles))
    time.sleep(2);send('A','screenshot',name='return-lobby')
