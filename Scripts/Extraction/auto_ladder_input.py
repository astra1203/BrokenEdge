"""Real forward input until automatic entry starts, then release before climbing."""
import math
import time


def walk_to(g, point, label, role='A'):
    """Leave an entry using real walking, including the last few centimetres."""
    send, wait, player = (g[k] for k in ('send','wait','player'))
    send(role,'walk',points=[point])
    wait(lambda: g['state'](role).get('walk_index')==1 and g['state'](role).get('walk_count')==1,label,10)
    for _ in range(80):
        p=player(role,role)['position']
        delta=[point[0]-p[0],point[1]-p[1]]
        if math.hypot(*delta)<3:break
        assert player('Server',role)['traversal']==0, label+' unexpectedly attached'
        send(role,'look',yaw=math.degrees(math.atan2(delta[1],delta[0])))
        send(role,'pulse',action='Move',duration=0)
        time.sleep(.06)
    g['check'](label+' reaches clear standing point',lambda: math.dist(player('Server',role)['position'][:2],point[:2])<5
               and not player('Server',role)['falling'],5)


def approach(g, label, role='A', expected=None, timeout=6, hold_after_start=0):
    send, wait, check, player = (g[k] for k in ('send', 'wait', 'check', 'player'))
    assert player('Server',role)['traversal']==0, label+' must start on foot'
    send(role,'pulse',action='Move',duration=timeout)
    try:
        wait(lambda: player('Server',role)['traversal'] in (3,4), label+' auto entry',timeout)
        if hold_after_start:
            time.sleep(hold_after_start)
            check(label+' held approach input does not exit again',lambda: player('Server',role)['traversal']==4,2)
    finally:
        send(role,'pulse',action='Move',duration=0)
    check(label+' attaches without E',lambda: all(player(peer,role)['traversal']==4 for peer in ('Server',role))
          and (not expected or player('Server',role)['traversal_ladder']==expected),5)
