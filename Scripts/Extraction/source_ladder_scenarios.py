"""Input-driven round trips at every enabled original-map climb actor."""
import hashlib
import json
import math
from pathlib import Path
import time
from auto_ladder_input import approach, walk_to


def run(g):
    send, check, wait, state, player = (g[k] for k in ('send','check','wait','state','player'))
    path = g['ROOT']/'Scripts/Traversal/source_bindings.json'
    binding = json.loads(path.read_text(encoding='utf-8'))
    assert binding.get('schema')==3 and binding.get('interaction')=='walk_toward_entry', 'Automatic entry volumes require rebinding'
    mapfile = g['ROOT']/'Content/Maps/SpaceCenter/L_SpaceCenterExtraction.umap'
    assert hashlib.sha256(mapfile.read_bytes()).hexdigest() == binding['map_sha256'], 'Rebind after changing the source map'
    (g['OUT']/'SourceBindings.json').write_bytes(path.read_bytes())
    for source in (path, Path(__file__), g['ROOT']/'Scripts/Extraction/auto_ladder_input.py'):
        g['report']['input_sha256'][source.relative_to(g['ROOT']).as_posix()] = hashlib.sha256(source.read_bytes()).hexdigest()
    rows = [r for r in binding['candidates'] if r['classification']=='usable']
    assert rows, 'No accepted original ladders'
    half = player('Server','A')['capsule_half']
    recording = g['args'].traversal_record
    send('A','fps',value=60)

    def place(p, yaw):
        send('Server','fixture',players=[dict(name='EX_A',position=[p[0],p[1],p[2]+half+3],health=100,shield=0,clear_trauma=True,yaw=yaw)])
        send('A','look',yaw=yaw)
        wait(lambda: math.dist(player('A','A')['position'][:2],p[:2])<5, 'original ladder placement converges')
        time.sleep(.3)

    for row in rows:
        sid = row['id']; points = row['points']; f = row['facing']
        yaw = math.degrees(math.atan2(f[1],f[0]))
        assert not row['approach_errors'], str(row['approach_errors'])
        place(row['approaches']['bottom'], yaw)
        focus = row['actor'].rsplit('.',1)[-1]
        check(sid+' begins outside entry',lambda: player('A','A')['ladder_focus']=='None' and player('Server','A')['traversal']==0)
        bottom, top = points['bottom'], points['top_exit']
        height = top[2]-bottom[2]
        distance = max(450,height*.85)
        middle = [bottom[0],bottom[1],bottom[2]+height*.5+100]
        camera = [middle[0]-f[0]*distance-f[1]*250,middle[1]-f[1]*distance+f[0]*250,middle[2]]
        send('B','world_camera',position=camera,target=middle,field_of_view=80)
        for peer in ('A','B'): send(peer,'screenshot',name='source-'+sid+'-approach')
        if recording:
            for peer in ('A','B'): send(peer,'record',enabled=True,name='source-'+sid)
        approach(g,sid+' bottom',expected=focus)
        start = player('Server','A')['position'][2]
        send('A','pulse',action='Move',duration=.35)
        check(sid+' climb up',lambda: player('Server','A')['position'][2]>start+25,4)
        time.sleep(.45)
        paused = player('Server','A')['position'][2]
        time.sleep(.4)
        check(sid+' pause',lambda: player('Server','A')['traversal']==4 and abs(player('Server','A')['position'][2]-paused)<3)
        for peer in ('A','B'):send(peer,'pose',target='EX_A',name='source-'+sid+'-paused')
        send('A','pulse',action='MoveBack',duration=.12)
        check(sid+' climb down',lambda: player('Server','A')['position'][2]<paused-10,4)
        duration=(row['original_endpoints']['LadderTopEndTransform'][2]-bottom[2])/150+4
        send('A','pulse',action='Move',duration=duration)
        wait(lambda: player('Server','A')['traversal'] in (0,5),sid+' top transition',duration+3)
        send('A','pulse',action='Move',duration=0)
        check(sid+' top landing',lambda: player('Server','A')['traversal']==0 and not player('Server','A')['falling']
              and math.dist(player('Server','A')['position'],[top[0],top[1],top[2]+half])<16,8)
        send('A','look',yaw=yaw+180)
        for peer in ('A','B'):send(peer,'screenshot',name='source-'+sid+'-top')
        check(sid+' top exit suppresses immediate re-entry',lambda: player('Server','A')['ladder_reentry_block']==focus)
        # Leave the top entry with real movement, then turn and walk back in.
        walk_to(g,row['approaches']['top'],sid+' leave top entry')
        check(sid+' entry reset',lambda: player('Server','A')['ladder_reentry_block']=='None')
        send('A','look',yaw=yaw+180)
        approach(g,sid+' top',expected=focus,hold_after_start=.8)
        send('A','pulse',action='MoveBack',duration=duration)
        wait(lambda: player('Server','A')['traversal'] in (0,5),sid+' bottom transition',duration+3)
        send('A','pulse',action='MoveBack',duration=0)
        end = points['bottom_exit']
        check(sid+' bottom landing',lambda: player('Server','A')['traversal']==0 and not player('Server','A')['falling']
              and math.dist(player('Server','A')['position'],[end[0],end[1],end[2]+half])<16,8)
        check(sid+' owner/observer weapon restored',lambda: all(not player(p,'A')['weapon_hidden'] for p in ('A','B')) and player('A','A')['fp_weapon_visible'])
        if recording:
            for peer in ('A','B'):send(peer,'record',enabled=False,name='source-'+sid)
    for i, (p,yaw) in enumerate([([-4003.65,-2196.24,360.33],90),([13682.41,-4576.30,360.33],-90),([-3208.19,594.69,1387.21],-90)]):
        place(p,yaw)
        check('Legacy furniture ladder '+str(i)+' has no prompt',lambda: player('A','A')['ladder_focus']=='None')
    g['report']['source_ladders'] = dict(result='passed',ids=[r['id'] for r in rows],
        unresolved=[r['id'] for r in binding['candidates'] if r['classification']!='usable'])
