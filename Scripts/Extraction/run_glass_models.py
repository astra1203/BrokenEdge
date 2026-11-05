"""Real combat input against authored glass; real kit drops plus explicit collectible visual fixtures."""
import hashlib
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
    g['report']['input_sha256']['Scripts/Extraction/run_glass_models.py']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    g['report']['scope']='Authored map panes; real Fire, knife and Alt+G input; position fixtures; real starting-gear drops; three explicit collectible visual fixtures.'
    check('Authored breakable glass exists on every peer',lambda:all(state(p).get('glass_count',0)>0 for p in peers))
    check('Construction-script panes leave no duplicate visual or collision',lambda:all(state(p).get('legacy_glass_visible')==0 and state(p).get('legacy_glass_blocking')==0 for p in peers))
    send('Server','glass_probe')
    wait(lambda:state('Server').get('glass_sites'),'reachable glass survey')
    sites=state('Server')['glass_sites']
    def pane(peer,ident):return next((r for r in state(peer).get('glass',[]) if r['id']==ident),{})
    def aim(point):
        eye=player('A','A')['view_position'];delta=[b-a for a,b in zip(eye,point)]
        send('A','look',yaw=math.degrees(math.atan2(delta[1],delta[0])),pitch=math.degrees(math.atan2(delta[2],math.hypot(*delta[:2]))))
    def place(site):
        observer=[site['standing'][i]+site['normal'][i]*300 for i in range(3)]
        observer[0]-=site['normal'][1]*200;observer[1]+=site['normal'][0]*200
        send('Server','fixture',players=[dict(name='EX_A',position=site['standing'],health=100,shield=0,clear_trauma=True),
                                        dict(name='EX_B',position=observer,health=100,shield=0,clear_trauma=True)])
        wait(lambda:math.dist(player('A','A')['position'][:2],site['standing'][:2])<5 and not player('A','A')['falling'],'standing at glass approach')
        aim(site['center']);time.sleep(.3)
        check('Intact '+site['id']+' blocks and renders on every peer',lambda:all(pane(p,site['id']).get('trace_blocked') and pane(p,site['id']).get('visible') for p in peers))
    def broken(site,label):
        check(label+' removes pane collision and visual on every peer',lambda:all(pane(p,site['id']).get('broken') and not pane(p,site['id']).get('visible',True) and pane(p,site['id']).get('collision',-1)==0 and not pane(p,site['id']).get('trace_blocked',True) for p in peers),10)
    shot=next(s for s in sites if s['distance']==350)
    place(shot)
    send('A','screenshot',name='glass-intact')
    accepted=player('Server','A')['accepted']
    send('A','pulse',action='Fire',duration=.2)
    check('Glass shot uses authoritative firearm path',lambda:player('Server','A')['accepted']>accepted)
    broken(shot,'Gunfire')
    send('A','screenshot',name='glass-shot')
    send('Server','glass_probe');sites=state('Server')['glass_sites']
    knife=next(s for s in sites if s['distance']==90 and s['id']!=shot['id'])
    place(knife)
    send('A','input_key',key='Three',pressed=True);send('A','input_key',key='Three',pressed=False)
    check('Knife equipped before glass strike',lambda:player('A','A')['knife_active'])
    ammo=player('Server','A')['ammo']
    send('A','pulse',action='Fire',duration=0)
    broken(knife,'Knife strike')
    check('Knife strike spends no ammunition',lambda:player('Server','A')['ammo']==ammo)
    send('A','input_key',key='One',pressed=True);send('A','input_key',key='One',pressed=False)
    send('Server','glass_probe');sites=state('Server')['glass_sites']
    grenade=next(s for s in sites if s['distance']==350 and s.get('blast_exposed') and s['id'] not in [shot['id'],knife['id']])
    place(grenade)
    observer=[grenade['standing'][i]+grenade['normal'][i]*1100 for i in range(3)]
    send('Server','fixture',players=[dict(name='EX_B',position=observer,health=100,shield=0,clear_trauma=True)])
    before=state('Server')['broken_glass_count']
    def frags():return sum(i['quantity'] for i in player('Server','A')['inventory']['items'] if i['definition']=='FragGrenade' and i['slot']!=-3)
    count=frags()
    send('A','look',yaw=math.degrees(math.atan2(grenade['center'][1]-grenade['standing'][1],grenade['center'][0]-grenade['standing'][0])),pitch=-80)
    send('A','input_key',key='LeftAlt',pressed=True)
    send('A','input_key',key='G',pressed=True);time.sleep(.25)
    send('A','input_key',key='G',pressed=False)
    send('A','input_key',key='LeftAlt',pressed=False)
    check('Real Alt+G spends exactly one starting grenade',lambda:frags()==count-1)
    check('Thrown grenade uses its real world mesh',lambda:any('/Extraction/Items/WorldModels/' in r.get('mesh','') and r.get('visible') for r in state('A').get('frag_grenades',[])),2)
    send('A','pulse',action='MoveBack',duration=2)
    check('Grenade explosion breaks nearby authored glass',lambda:state('Server')['broken_glass_count']>before,10)
    check('Explosion glass state agrees on every peer',lambda:len({state(p)['broken_glass_count'] for p in peers})==1)
    if grenade['id']=='A5BAD90F63F81653292D465A38A4D93A':
        check('Solid upper floor still shields the upstairs window',lambda:all(pane(p,'9F914AEBC2C02B9C548852ADEBBCC0F6').get('visible') and not pane(p,'9F914AEBC2C02B9C548852ADEBBCC0F6').get('broken') for p in peers))
    time.sleep(2)
    send('A','screenshot',name='glass-grenade')

    # Keep the grenade and bags usable until their corresponding real drop operation.
    drop_start=[-1500,-300,460] if g['args'].scenario=='scar' else [-4500,3000,300]
    observer=[drop_start[0]-150,drop_start[1]+200,drop_start[2]]
    send('Server','fixture',players=[dict(name='EX_A',position=drop_start,health=100,shield=0,clear_trauma=True),
                                    dict(name='EX_B',position=observer,health=100,shield=0,clear_trauma=True)])
    wait(lambda:math.dist(player('A','A')['position'][:2],drop_start[:2])<5 and not player('A','A')['falling'],'drop location grounded')
    drop_start=list(player('Server','A')['position'])
    send('A','look',yaw=0,pitch=-30)
    send('A','inventory',open=True)
    for index,definition in enumerate(['Helmet2','Vest2','FragGrenade','Bag_Test']):
        location=[drop_start[0],drop_start[1]+index*100,drop_start[2]]
        send('Server','fixture',players=[dict(name='EX_A',position=location,health=100,shield=0,clear_trauma=True)])
        wait(lambda:math.dist(player('A','A')['position'][:2],location[:2])<5 and not player('A','A')['falling'],'next real drop position')
        wait(lambda:player('A','A')['inventory']==player('Server','A')['inventory'],'drop inventory sync')
        entry=next(i for i in player('Server','A')['inventory']['items'] if i['definition']==definition)
        old={r['id'] for r in state('Server')['loot']}
        sequence=state('A').get('item_sequence',0)
        send('A','item',kind='drop',item=entry['id'],quantity=1)
        wait(lambda:state('A').get('item_sequence',0)>sequence,'drop acknowledged')
        assert not state('A').get('item_message'),state('A').get('item_message')
        wait(lambda:any(r['id'] not in old and r['definition']==definition for r in state('Server')['loot']),'ground actor created')
        ident=next(r['id'] for r in state('Server')['loot'] if r['id'] not in old and r['definition']==definition)
        def visual(peer):
            row=next((r for r in state(peer)['loot'] if r['id']==ident),{})
            return any('/Extraction/Items/WorldModels/' in m['mesh'] and 1<max(m['extent'])*2<250 for m in row.get('models',[]))
        check(definition+' real drop has a correctly sized model on every peer',lambda:all(visual(p) for p in peers))
    send('A','inventory',open=False)
    for index,definition in enumerate(['Col_15030040003','Col_15080090002','Electronics']):
        p=list(drop_start);p[0]+=180;p[1]+=index*100;p[2]+=20-player('Server','A')['capsule_half']
        send('Server','spawn_loot',loose=True,definition=definition,position=p,label='Model visual fixture '+definition)
        check(definition+' collectible fixture has a real model on every peer',lambda d=definition:all(any(r['definition']==d and any('/Extraction/Items/WorldModels/' in m['mesh'] for m in r.get('models',[])) for r in state(peer)['loot']) for peer in peers))
    send('A','world_camera',position=[drop_start[0]+440,drop_start[1]+150,drop_start[2]+300],
         target=[drop_start[0]+120,drop_start[1]+150,drop_start[2]-70],field_of_view=65)
    time.sleep(1)
    send('A','screenshot',name='ground-item-models')
    send('A','camera',target='')

if __name__=='__main__':
    sys.argv.append('--starting-kit-validation')
    scar_scenarios.run=soviet_scenarios.run=run
    runpy.run_path(str(Path(__file__).with_name('run_network.py')),run_name='__main__')
