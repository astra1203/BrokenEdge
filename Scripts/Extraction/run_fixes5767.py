"""Focused multiplayer regressions for grid swaps, pockets and authored maintenance ladders."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import runpy
import sys
import time
import scar_scenarios
import soviet_scenarios
from auto_ladder_input import approach, walk_to

parser=argparse.ArgumentParser(add_help=False)
parser.add_argument('--fix-focus',choices=['inventory','inventory-loadout','ladders','ladders-glass'],required=True)
options,remaining=parser.parse_known_args()
sys.argv=[sys.argv[0],*remaining,'--starting-kit-validation']

def run(g):
    send,check,wait,state,player=(g[k] for k in ['send','check','wait','state','player'])
    g['report']['input_sha256']['Scripts/Extraction/run_fixes5767.py']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    g['report']['scope']='Real input and authoritative item requests; starting kit unchanged; position fixtures for ladder approaches.'
    peers=['Server',*g['ROLES']]
    def items():return player('Server','A')['inventory']['items']
    def item(ident):return next(i for i in items() if i['id']==ident)
    def request(ident,allow_error=False,**fields):
        fields.setdefault('auto',False)
        wait(lambda:player('A','A')['inventory']==player('Server','A')['inventory'],'inventory sync')
        sequence=state('A').get('item_sequence',0)
        send('A','item',item=ident,**fields)
        wait(lambda:state('A').get('item_sequence',0)>sequence,'item acknowledgement')
        message=state('A').get('item_message','')
        if not allow_error:assert not message,message
        return message
    if options.fix_focus.startswith('inventory'):
        check('New raid has five permanent pockets',lambda:player('Server','A')['inventory']['personal_pockets'])
        check('New raid has a rig and two usable grenades',lambda:any(i['slot']==4 for i in items())
              and sum(i['quantity'] for i in items() if i['definition']=='FragGrenade' and i['slot']!=-3)==2)
        send('A','inventory',open=True)
        wait(lambda:player('A','A')['inventory_open'],'inventory opens')
        sniper=next(i.copy() for i in items() if i['definition']=='BoltSniper')
        shotgun=next(i.copy() for i in items() if i['definition']=='PumpShotgun')
        before={i['id']:i.copy() for i in items()}
        def card(ident):return next((c for c in state('A').get('ui_cards',[]) if c['id']==ident and not c['loot'] and c['equipment_slot']<0),None)
        wait(lambda:card(sniper['id']) and card(shotgun['id']),'actual Slate item geometry')
        for _ in range(12):
            if all(155<card(i)['y']<600 for i in [sniper['id'],shotgun['id']]):break
            send('A','mouse',x=550,y=450,action='wheel',delta=-2)
            time.sleep(.15)
        a,b=card(sniper['id']),card(shotgun['id'])
        assert all(150<c['y']<620 for c in [a,b]),(a,b)
        send('A','mouse',x=a['x'],y=a['y'],action='down')
        send('A','mouse',x=a['x']+12,y=a['y'],action='move')
        send('A','mouse',x=b['x'],y=b['y'],action='move')
        send('A','screenshot',name='swap-valid-preview')
        send('A','mouse',x=b['x'],y=b['y'],action='up')
        check('Mouse drop swaps both long guns',lambda:all(item(a['id'])[k]==b[k] for a,b in [(sniper,shotgun),(shotgun,sniper)] for k in ['x','y','slot']))
        check('Mouse swap preserves all third items',lambda:all(item(k)==v for k,v in before.items() if k not in [sniper['id'],shotgun['id']]))
        for original in [sniper,shotgun]:
            check(original['definition']+' keeps magazine and attachments',lambda o=original:all(item(o['id'])[k]==o[k] for k in ['magazine','ammo_type','attachments','quantity']))
        request(sniper['id'],kind='swap',swap_with=shotgun['id'],quantity=sniper['quantity'],slot=-1,x=sniper['x'],y=sniper['y'])
        settled=player('Server','A')['inventory']
        send('A','item',item=sniper['id'],duplicate=True)
        time.sleep(.4)
        check('Replayed swap does not swap again',lambda:player('Server','A')['inventory']==settled)
        pocket_items=[i.copy() for i in items() if i['definition'] in ['FragGrenade','Ammo55645','Ammo76251','Ammo_338']][:5]
        assert len(pocket_items)==5,pocket_items
        for x,entry in enumerate(pocket_items):request(entry['id'],slot=-4,x=x,y=0,quantity=entry['quantity'])
        check('All five separate pockets accept single-cell items',lambda:sorted(i['x'] for i in items() if i['slot']==-4)==list(range(5)))
        snapshot=player('Server','A')['inventory']
        assert request(sniper['id'],allow_error=True,slot=-4,x=0,y=0,quantity=1)
        check('A long gun cannot span pockets',lambda:player('Server','A')['inventory']==snapshot)
        assert request(pocket_items[0]['id'],allow_error=True,slot=-4,x=5,y=0,quantity=pocket_items[0]['quantity'])
        check('A sixth pocket is rejected without losing items',lambda:player('Server','A')['inventory']==snapshot)
        send('A','screenshot',name='five-independent-pockets')
        for entry in pocket_items:request(entry['id'],slot=entry['slot'],x=entry['x'],y=entry['y'],quantity=entry['quantity'])
        send('A','inventory',open=False)
        if options.fix_focus=='inventory-loadout':
            import run_test_loadout
            run_test_loadout.run(g)
        return

    check('Three Soviet maintenance ladders are present on every peer',lambda:all(len(state(p).get('soviet_ladders',[]))==3 for p in peers))
    rows=state('Server')['soviet_ladders']
    half=player('Server','A')['capsule_half']
    for row in rows:
        yaw=row['yaw'];f=[math.cos(math.radians(yaw)),math.sin(math.radians(yaw)),0]
        start=[row['bottom'][i]-f[i]*170 for i in range(3)]
        send('Server','fixture',players=[dict(name='EX_A',position=[start[0],start[1],start[2]+half+3],health=100,shield=0,clear_trauma=True)])
        send('A','look',yaw=yaw,pitch=0)
        wait(lambda:math.dist(player('A','A')['position'][:2],start[:2])<5,row['id']+' approach position')
        approach(g,row['id']+' bottom',expected=row['name'])
        z=player('Server','A')['position'][2]
        send('A','pulse',action='Move',duration=.4)
        check(row['id']+' W climbs upward',lambda:player('Server','A')['position'][2]>z+25)
        time.sleep(.6);paused=player('Server','A')['position'][2];time.sleep(.4)
        check(row['id']+' released movement stays attached',lambda:player('Server','A')['traversal']==4 and abs(player('Server','A')['position'][2]-paused)<3)
        send('A','pose',target='EX_A',name=row['id']+'-climb')
        send('A','screenshot',name=row['id']+'-climb')
        right=[-f[1],f[0],0]
        camera=[row['bottom'][i]-f[i]*500+right[i]*220 for i in range(3)];camera[2]+=380
        target=[row['bottom'][i]+f[i]*55 for i in range(3)];target[2]+=250
        send('A','world_camera',position=camera,target=target,field_of_view=70)
        time.sleep(.3)
        send('A','screenshot',name=row['id']+'-world-ladder')
        send('A','camera',target='')
        duration=(row['top'][2]-row['bottom'][2])/150+5
        send('A','pulse',action='Move',duration=duration)
        wait(lambda:player('Server','A')['traversal'] in [0,5],row['id']+' top transition',duration+3)
        send('A','pulse',action='Move',duration=0)
        check(row['id']+' reaches upper walkway standing',lambda:all(player(p,'A')['traversal']==0 and not player(p,'A')['falling'] and not player(p,'A')['crouched'] for p in ['Server','A'])
              and math.dist(player('Server','A')['position'],[row['top_exit'][0],row['top_exit'][1],row['top_exit'][2]+half])<20,8)
        leave=[row['top_exit'][i]+f[i]*180 for i in range(3)]
        walk_to(g,leave,row['id']+' leave top trigger')
        send('A','look',yaw=yaw+180)
        approach(g,row['id']+' top',expected=row['name'],hold_after_start=.8)
        send('A','pulse',action='MoveBack',duration=duration)
        wait(lambda:player('Server','A')['traversal'] in [0,5],row['id']+' bottom transition',duration+3)
        send('A','pulse',action='MoveBack',duration=0)
        check(row['id']+' S returns to ground',lambda:all(player(p,'A')['traversal']==0 and not player(p,'A')['falling'] for p in ['Server','A'])
              and math.dist(player('Server','A')['position'],[row['bottom_exit'][0],row['bottom_exit'][1],row['bottom_exit'][2]+half])<20,8)
        check(row['id']+' never automatically crouches',lambda:all(not player(p,'A')['crouched'] for p in ['Server','A']))
        walk_to(g,start,row['id']+' leave bottom trigger')
        send('A','look',yaw=yaw,pitch=0)
        approach(g,row['id']+' jump entry',expected=row['name'])
        send('A','pulse',action='Move',duration=.7)
        time.sleep(.9)
        send('A','pulse',action='Jump',duration=.12)
        check(row['id']+' Space detaches from ladder',lambda:all(player(p,'A')['traversal']==0 for p in ['Server','A']))
        check(row['id']+' jump detach lands standing',lambda:not player('Server','A')['falling'] and not player('Server','A')['crouched'],8)
    if options.fix_focus=='ladders-glass':
        import run_glass_models
        run_glass_models.run(g)

scar_scenarios.run=soviet_scenarios.run=run
runpy.run_path(str(Path(__file__).with_name('run_network.py')),run_name='__main__')
