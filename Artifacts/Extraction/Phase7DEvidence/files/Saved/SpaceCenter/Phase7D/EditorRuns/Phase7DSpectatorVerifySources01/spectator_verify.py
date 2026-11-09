import json,math

def run(g):
    state,player,send,check,wait=(g[n] for n in ['state','player','send','check','wait'])
    g['report']['diagnostic_only']='Focused fixture placements verify far spectator replication and camera recovery; not input traversal acceptance.'
    exit_at=state('Server')['exit_position']
    send('Server','fixture',players=[{'name':'EX_A','position':[exit_at[0],exit_at[1],exit_at[2]+90],'health':100,'shield':0},
        {'name':'EX_B','position':[-1500,-300,456],'health':100,'shield':0}])
    check('A extracts for the focused spectator verification',lambda:state('A')['account']['outcome']==1,25)
    wait(lambda:state('A').get('spectated')=='EX_B','A selects B')
    def follows():
        target=player('A','B').get('position')
        return (state('A').get('spectated')=='EX_B' and target is not None
            and math.dist(target,player('Server','B')['position'])<25
            and math.dist(state('A')['view_position'],target)<500)
    for label,position in [('far-east',[24100,15600,165]),('old-interior',[-1500,-300,456])]:
        send('Server','fixture',players=[{'name':'EX_B','position':position,'health':100,'shield':0}])
        check('B reaches '+label+' on server and owner',lambda:math.dist(player('Server','B')['position'][:2],position[:2])<25
            and math.dist(player('B','B')['position'],player('Server','B')['position'])<25)
        check('Spectator receives B and follows at '+label,follows)
        send('A','screenshot',name='spectator-'+label)
    send('Server','end')
    check('Focused spectator verification returns to lobby',lambda:all(state(r).get('map')=='/Game/Maps/Lobby' for r in g['processes']),40)
