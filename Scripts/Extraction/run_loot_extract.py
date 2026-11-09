"""Authored loot -> production transfer -> timed settlement in either extraction map.

Positions are explicit fixtures. SpaceCenter levers use real F input. No inventory
injection, schedule changes, outcome forcing, or continuous traversal claim.
"""
import hashlib
import json
import math
from pathlib import Path
import runpy
import sys
import time
import scar_scenarios
import soviet_scenarios


def run(g):
    send,check,wait,state,player=(g[k] for k in ['send','check','wait','state','player'])
    peers=['Server',*g['ROLES']]
    g['report']['input_sha256']['Scripts/Extraction/run_loot_extract.py']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    g['report']['scope']='Authored container, production search/transfer, real SpaceCenter lever input and timed settlement; position fixtures only; no item injection or continuous-route claim.'
    origin=player('Server','A')['position']
    candidates=sorted([r for r in state('Server')['loot'] if not r['loose'] and not r['empty'] and r.get('source_point') not in ('None','')],key=lambda r:math.dist(origin,r['position']))
    selected=None
    for box in candidates[:12]:
        for angle in (0,90,180,270):
            rad=math.radians(angle);p=box['position']
            send('Server','fixture',players=[dict(name='EX_A',position=[p[0]+130*math.cos(rad),p[1]+130*math.sin(rad),p[2]+98],health=100,shield=0,clear_trauma=True)])
            send('A','look',yaw=angle+180,pitch=-20)
            time.sleep(.5)
            send('A','open_loot',target=box['id'])
            time.sleep(.4)
            if player('A','A').get('loot_id')==box['id']:
                selected=box;break
            send('A','inventory',open=False)
        if selected:break
    check('Authored container opens through production search request',lambda:selected is not None)
    g['report']['searched_container']=selected
    wait(lambda:player('A','A')['loot_view']['items'] and all(i['definition']!='None' for i in player('A','A')['loot_view']['items']),'Search reveals real contents')
    send('A','screenshot',name='authored-search')
    transferred=None
    for item in player('A','A')['loot_view']['items']:
        sequence=state('A').get('item_sequence',0)
        send('A','item',item=item['id'],from_loot=True,quantity=item['quantity'],auto=True)
        wait(lambda:state('A').get('item_sequence',0)>sequence,'Transfer acknowledged')
        if not state('A').get('item_message'):
            transferred=item;break
    check('Searched item transferred without changing identity',lambda:transferred is not None and any(i['id']==transferred['id'] for i in player('Server','A')['inventory']['items']))
    g['report']['transferred_item']=transferred
    send('A','inventory',open=False)
    exits=state('Server')['exits']
    selected_exit=(next(r for r in exits if r['available']) if g['args'].scenario=='soviet'
                   else next(r for r in exits if r['id']=='OfficialExitSquare'))
    g['report']['exit_original']=selected_exit
    if g['args'].scenario=='scar':
        anchors_file=g['ROOT']/'Artifacts/Extraction/RestorationEvidence/SpawnExit/OfficialSpawnExitAnchors.json'
        anchors=json.loads(anchors_file.read_text(encoding='utf8'))
        def available():return any(r['id']==selected_exit['id'] and r['available'] for r in state('Server')['exits'])
        attempts=g['report']['lever_inputs']=[]
        for lever in [r for r in anchors['exit_triggers'] if r['actor'].startswith('BP_ExitTriggerSquare')]:
            for angle in (0,90,180,270):
                p=lever['world'];rad=math.radians(angle)
                location=[p[0]+100*math.cos(rad),p[1]+100*math.sin(rad),p[2]+98]
                send('Server','fixture',players=[dict(name='EX_A',position=location,health=100,shield=0,clear_trauma=True)])
                send('A','look',yaw=angle+180,pitch=-20)
                time.sleep(.6)
                send('A','input_key',key='F',pressed=True)
                send('A','input_key',key='F',pressed=False)
                time.sleep(.3)
                attempts.append(dict(lever=lever['actor'],position=location,message=state('A').get('item_message')))
                if available():break
            if available():break
        check('Real lever input opens the authored plaza exit on every peer',lambda:all(any(r['id']==selected_exit['id'] and r['available'] for r in state(p)['exits']) for p in peers))
    centre=selected_exit['bounds_center']
    send('Server','fixture',players=[dict(name='EX_'+role,position=[centre[0]+(index-.5)*120,centre[1],centre[2]],health=100,shield=0,clear_trauma=True) for index,role in enumerate(g['ROLES'])])
    check('Both players enter real exit hold',lambda:all(player('Server',r).get('extract_at',0)>0 for r in g['ROLES']))
    send('A','screenshot',name='real-exit-hold')
    check('Both players settle extraction durably',lambda:all(state(r).get('account',{}).get('outcome')==1 for r in g['ROLES']),selected_exit['hold_seconds']+15)
    check('Return to lobby after settlement',lambda:all(state(p).get('map')=='/Game/Maps/Lobby' for p in peers),40)
    check('Searched item GUID and quantity persist in warehouse',lambda:sum(i['quantity'] for section in ['stash','overflow'] for i in state('A')['account'][section]['items'] if i['id']==transferred['id'])==transferred['quantity'])
    send('A','screenshot',name='settled-lobby')


if __name__=='__main__':
    sys.argv.append('--starting-kit-validation')
    scar_scenarios.run=soviet_scenarios.run=run
    runpy.run_path(str(Path(__file__).with_name('run_network.py')),run_name='__main__')
