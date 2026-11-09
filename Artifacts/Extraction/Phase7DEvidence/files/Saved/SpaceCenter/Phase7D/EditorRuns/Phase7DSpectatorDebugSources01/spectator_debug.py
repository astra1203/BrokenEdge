import json,time

def run(g):
    state,player,send,check,wait=(g[n] for n in ['state','player','send','check','wait'])
    out=g['OUT'];g['report']['diagnostic_only']='Teleport target to reproduce remote spectator relevancy; not east traversal acceptance.'
    exit_at=state('Server')['exit_position']
    send('Server','fixture',players=[{'name':'EX_A','position':[exit_at[0],exit_at[1],exit_at[2]+90],'health':100,'shield':0},
        {'name':'EX_B','position':[-1500,-300,456],'health':100,'shield':0}])
    check('A extracts for the focused spectator diagnostic',lambda:state('A')['account']['outcome']==1,25)
    wait(lambda:state('A').get('spectated')=='EX_B','A selects B')
    send('Server','fixture',players=[{'name':'EX_B','position':[24100,15600,165],'health':100,'shield':0}])
    check('B target exists at the far-east endpoint on server and owner',lambda:player('Server','B')['position'][0]>24000 and player('B','B')['position'][0]>24000)
    time.sleep(20)
    (out/'DiagnosticState.json').write_text(json.dumps({r:state(r) for r in ['Server','A','B']},indent=2),encoding='utf-8')
    g['save']();print('SPECTATOR_DEBUG_READY',g['report']['processes']['Server']['pid'],flush=True)
    wait(lambda:(out/'FinishDebug.txt').exists(),'spectator debugger evidence',600)
