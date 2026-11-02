"""Authority/replication/HUD rule checks at the authored plaza exit.

Positions and exit schedules are explicit Development-driver fixtures. This
scenario does not prove spawn-to-exit traversal, official timing or settlement.
The runner must start two clients in an active raid with no guards and render A.
"""
import hashlib
from pathlib import Path


def complete_png(path):
    """Only accept the screenshot after its final PNG chunk has reached disk."""
    try:
        data = path.read_bytes()
    except OSError:
        return False
    return (len(data) > 128 and data.startswith(b'\x89PNG\r\n\x1a\n')
            and data.endswith(b'\x00\x00\x00\x00IEND\xae\x42\x60\x82'))


def default_feedback(value):
    return (value.get('extract_exit_id') == 'None' and value.get('extract_blocked_reason') == ''
            and value.get('extract_at') == 0)


def feedback_matches(read_player, role, expected, peers=('Server', 'A', 'B')):
    """Private exit fields reach authority/owner only; Life remains public."""
    for peer in peers:
        value = read_player(peer, role)
        if value.get('life') != 0:
            return False
        if not (expected(value) if peer in ('Server', role) else default_feedback(value)):
            return False
    return True


def run(g):
    send, check, wait, state, player = (g[key] for key in ('send', 'check', 'wait', 'state', 'player'))
    peers = ('Server', 'A', 'B')
    assert all(role in g['processes'] for role in peers), 'Exit feedback requires a server and clients A/B'
    assert 'A' in g['rendered_roles'], 'Client A must render actual HUD controls and screenshots'
    g['report']['input_sha256']['Scripts/Extraction/exit_feedback_scenarios.py'] = hashlib.sha256(
        Path(__file__).read_bytes()).hexdigest()
    check('Exit-feedback fixture starts in an active raid with two live players and no guards',
          lambda: state('Server').get('phase') == 1 and not state('Server').get('guards')
          and all(player('Server', role).get('life') == 0 for role in ('A', 'B')))
    candidates = [entry for entry in state('Server')['exits']
                  if '308' in entry['id'] or '广场' in entry['label']]
    assert candidates, 'The formal map must export its authored plaza/308 exit'
    selected = sorted(candidates, key=lambda entry: entry['id'])[0]
    exit_id, label = selected['id'], selected['label']
    centre = selected['bounds_center']
    assert len(centre) == 3 and len(selected['half_extent']) == 3, 'Driver must export authored exit bounds'
    assert min(selected['half_extent']) > 70, 'Plaza fixture needs space for two separate character centres'
    outside = {role: list(player('Server', role)['position']) for role in ('A', 'B')}
    inside = {'A': [centre[0] - 60, centre[1], centre[2]],
              'B': [centre[0] + 60, centre[1], centre[2]]}
    evidence = g['report']['exit_feedback'] = dict(
        scope='teleported rule/replication/rendered-HUD fixtures; not continuous-route or settlement evidence',
        exit_id=exit_id, label=label, original_exit=selected,
        outside_positions=outside, inside_positions=inside, screenshots=[])

    def command(role, op, **fields):
        error = send(role, op, **fields)
        assert not error, f'{role} {op}: {error}'

    def exit_state(role):
        return next((entry for entry in state(role).get('exits', []) if entry['id'] == exit_id), {})

    def configure(**fields):
        command('Server', 'exit_config', exit_id=exit_id, **fields)
        expected = {key: value for key, value in fields.items() if key != 'reset_start'}
        wait(lambda: all(all(exit_state(peer).get(key) == value for key, value in expected.items())
                         for peer in peers), 'Exit configuration replicates to both clients', 15)

    def place(positions):
        command('Server', 'fixture', players=[dict(name='EX_' + role, position=position,
                health=100, shield=0, clear_trauma=True) for role, position in positions.items()])

    def cleared(role):
        return feedback_matches(player, role, default_feedback, peers)

    def blocked(role, reason):
        return feedback_matches(player, role, lambda value:
            value.get('extract_exit_id') == exit_id and reason in value.get('extract_blocked_reason', '')
            and value.get('extract_at') == 0, peers)

    def counting(role):
        return feedback_matches(player, role, lambda value:
            value.get('extract_exit_id') == exit_id and value.get('extract_blocked_reason') == ''
            and value.get('extract_at', 0) > 0, peers)

    def hud_matches(role, text, progress):
        hud = state(role).get('extraction_hud', {})
        return (label in hud.get('countdown_text', '') and text in hud.get('countdown_text', '')
                and hud.get('progress_visible') is progress
                and (not progress or 0 <= hud.get('progress_percent', -1) <= 1))

    def screenshot(name):
        command('A', 'screenshot', name=name)
        path = g['OUT'] / ('A-' + name + '.png')
        wait(lambda: complete_png(path), 'Complete rendered HUD screenshot ' + name, 15)
        evidence['screenshots'].append(dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))

    # Keep production hold time comfortably longer than all fixture assertions.
    configure(opens_after=300, closes_after=0, hold_seconds=300, capacity=2, used=0, reset_start=True)
    check('Both spawn positions are outside all exit areas before the rule fixture',
          lambda: all(cleared(role) for role in ('A', 'B')))
    place({'A': inside['A']})
    check('Unopened plaza reaches authority and owner only, with observer feedback isolated',
          lambda: blocked('A', '秒后开放') and cleared('B'), 15)
    check('Rendered central HUD names the unopened plaza and hides extraction progress',
          lambda: hud_matches('A', '秒后开放', False), 10)
    screenshot('exit-feedback-unopened')

    configure(opens_after=0)
    check('Opening starts the hold on authority and owner while observer feedback stays private',
          lambda: counting('A') and cleared('B'), 15)
    first_deadline = player('Server', 'A')['extract_at']
    check('Rendered central HUD shows the plaza countdown and active progress',
          lambda: hud_matches('A', '正在撤离', True), 10)
    screenshot('exit-feedback-countdown')

    place({'B': inside['B']})
    check('Each owner receives an independent private deadline without resetting the other player',
          lambda: counting('A') and counting('B')
          and player('Server', 'A')['extract_at'] == first_deadline
          and player('Server', 'B')['extract_at'] > first_deadline, 15)
    evidence['independent_deadlines'] = {role: player('Server', role)['extract_at'] for role in ('A', 'B')}

    configure(capacity=1, used=1)
    check('Capacity cancels both holds for authority and each owner without leaking feedback to observers',
          lambda: all(blocked(role, '名额已用尽') for role in ('A', 'B')), 15)
    check('Rendered central HUD shows exhausted capacity and hides progress',
          lambda: hud_matches('A', '名额已用尽', False), 10)
    screenshot('exit-feedback-capacity')

    place({'A': outside['A']})
    check('Leaving clears only that player while the remaining player keeps the blocked exit',
          lambda: cleared('A') and blocked('B', '名额已用尽'), 15)
    check('Rendered central HUD and progress disappear after leaving',
          lambda: state('A').get('extraction_hud', {}).get('countdown_text') == ''
          and state('A').get('extraction_hud', {}).get('progress_visible') is False, 10)
    screenshot('exit-feedback-left-area')

    configure(capacity=2, used=0)
    check('Remaining player restarts a fresh hold while the departed player stays clear',
          lambda: cleared('A') and counting('B')
          and player('Server', 'B')['extract_at'] > evidence['independent_deadlines']['B'], 15)
    # The closing boundary must cancel the ongoing hold while retaining private feedback.
    configure(opens_after=0, closes_after=1, reset_start=True)
    check('Closing the occupied area cancels the remaining hold and retains its closed reason',
          lambda: cleared('A') and blocked('B', '已关闭'), 15)
    place({'B': outside['B']})
    check('Both players leave with cleared feedback and neither is falsely marked extracted',
          lambda: all(cleared(role) for role in ('A', 'B')), 15)
    evidence['result'] = 'passed'
