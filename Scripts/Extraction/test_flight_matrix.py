"""Pure Python tests: no Unreal, network process, fixture or real input is run."""
import copy
import json
import math
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import flight_matrix_scenarios as matrix


def history():
    return dict(old=dict(camera=[0, 0, 64], actor=[0, 0, 0], target_actor=[1000, 0, 0]),
                new=dict(camera=[300, 0, 64], actor=[300, 0, 0], target_actor=[1000, 300, 0]),
                sources={})


def character(role, position=None):
    return dict(name='EX_' + role, pawn_name='Pawn_' + role,
                pawn_class='/Game/Characters/Stinger/Blueprints/BP_StingerCharacter.BP_StingerCharacter_C',
                health=80, shield=0, ammo=30, carried_ammo=0, accepted=0, rejected=0,
                bullet_sequence=0, guard_kills=0, life=0, bleeding=False, fracture=False,
                weapon_item='Rifle_' + role, position=position or [0, 0, 0], falling=False,
                mechanics_ready=True)


def probe(case, timestamp=5):
    hit = dict(blocking=True, actor='Pawn_B', component='CharacterMesh0', component_path='/World/Pawn_B.Mesh',
               bone='Spine1', location=[1000, 0, 10], impact_point=[1000, 0, 10], impact_normal=[-1, 0, 0],
               start_penetrating=False, distance=990)
    body = list(case['positions']['B'])
    body[2] += 10
    delta = [b-a for a, b in zip(case['camera'], body)]
    length = math.sqrt(sum(x*x for x in delta))
    p = dict(read_only_probe=True, target_name='EX_B', target_actor='Pawn_B', shooter_actor='Pawn_A',
             configured_head=False, configured_bone='None', configured_body_center=False,
             muzzle_probe_available=True, camera_position=case['camera'],
             control_direction=[x/length for x in delta],
             points={name: dict(available=True, position=body, **{key: copy.deepcopy(hit) for key in matrix.QUERIES})
                     for name in ('body', 'head', 'spine2', 'selected')})
    return dict(role='A', target='EX_B', world_time=timestamp, aim_diagnostics=p)


class MatrixBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.case = matrix.matrix_cases(history())[0]
        self.record = probe(self.case)

    def matches(self, record=None):
        return matrix.matching_probe(record or self.record, self.case, 'Pawn_A', 'Pawn_B', 4)

    def test_four_independent_observer_target_combinations(self):
        cases = matrix.matrix_cases(history())
        self.assertEqual([v['id'] for v in cases], ['old-old', 'old-new', 'new-old', 'new-new'])
        self.assertEqual(cases[1]['camera'], history()['old']['camera'])
        self.assertEqual(cases[1]['positions']['B'], history()['new']['target_actor'])
        self.assertEqual(cases[2]['positions']['A'], history()['new']['actor'])

    def test_valid_probe_and_arbitrary_blocker_are_observations_not_clearance_assertions(self):
        self.assertTrue(self.matches())
        hit = self.record['aim_diagnostics']['points']['body']['camera_complex']
        hit.update(actor='Platform_1', component='Instances', component_path='/World/Platform_1.Instances', bone='None')
        self.assertTrue(self.matches())
        self.assertEqual(hit['actor'], 'Platform_1')

    def test_stale_record_and_wrong_identity_are_not_accepted(self):
        self.record['world_time'] = 4
        self.assertFalse(self.matches())
        self.record['world_time'] = 5
        self.record['aim_diagnostics']['target_actor'] = 'OtherPawn'
        self.assertFalse(self.matches())

    def test_camera_offset_and_wrong_target_position_are_not_accepted(self):
        for key in ('camera', 'target'):
            with self.subTest(key=key):
                record = probe(self.case)
                if key == 'camera':
                    record['aim_diagnostics']['camera_position'] = [1, 0, 64]
                else:
                    record['aim_diagnostics']['points']['body']['position'] = [1005, 0, 10]
                self.assertFalse(self.matches(record))

    def test_missing_complex_or_muzzle_result_is_not_silently_accepted(self):
        for key in ('camera_complex', 'muzzle_complex_sphere'):
            record = probe(self.case)
            del record['aim_diagnostics']['points']['body'][key]
            self.assertFalse(self.matches(record))

    def test_head_aim_or_stale_control_rotation_is_not_body_replay(self):
        self.record['aim_diagnostics']['configured_head'] = True
        self.assertFalse(self.matches())
        self.record['aim_diagnostics']['configured_head'] = False
        self.record['aim_diagnostics']['control_direction'] = [-1, 0, 0]
        self.assertFalse(self.matches())

    def test_partial_append_ignored_but_corrupt_complete_line_rejected(self):
        self.assertEqual(matrix.read_complete_records('{"n":1}\n{"n":'), [{'n': 1}])
        with self.assertRaises(json.JSONDecodeError):
            matrix.read_complete_records('{broken}\n')

    def test_fixture_preserves_exact_current_health_shield_and_omits_resource_mutations(self):
        snapshot = dict(players=[character('A'), character('B')])
        snapshot['players'][0]['health'] = 17
        snapshot['players'][1]['shield'] = 42
        values = matrix.fixture_players(snapshot, self.case['positions'])
        self.assertEqual(values[0]['health'], 17)
        self.assertEqual(values[1]['shield'], 42)
        self.assertEqual(set(values[0]), {'name', 'position', 'health', 'shield'})

    def test_falling_and_drift_rejected(self):
        s = dict(players=[character(r, self.case['positions'][r]) for r in matrix.ROLES])
        self.assertTrue(matrix.positions_match(matrix.position_observation(s, self.case['positions'])))
        s['players'][1]['falling'] = True
        self.assertFalse(matrix.positions_match(matrix.position_observation(s, self.case['positions'])))
        s['players'][1]['falling'] = False
        s['players'][1]['position'] = [1000, 0, -3]
        self.assertFalse(matrix.positions_match(matrix.position_observation(s, self.case['positions'])))

    def test_missing_remote_pawn_waits_then_requires_real_coordinates(self):
        s = dict(players=[character('A', self.case['positions']['A']), dict(name='EX_B', life=0)])
        observed = matrix.position_observation(s, self.case['positions'])
        self.assertFalse(matrix.positions_match(observed))
        self.assertFalse(observed['B']['ready'])
        self.assertEqual(set(observed['B']['missing_fields']), {'position','falling','pawn_name'})
        self.assertIsNone(observed['B']['actual'])
        s['players'][1] = character('B', self.case['positions']['B'])
        self.assertTrue(matrix.positions_match(matrix.position_observation(s, self.case['positions'])))
        s['players'][1]['position'] = [1000, 0, -3]
        self.assertFalse(matrix.positions_match(matrix.position_observation(s, self.case['positions'])))

    def test_missing_player_or_falling_or_identity_never_counts_as_ready(self):
        self.assertFalse(matrix.positions_match({}))
        self.assertFalse(matrix.positions_match(matrix.position_observation({}, self.case['positions'])))
        for field in ('position', 'falling', 'pawn_name'):
            s = dict(players=[character(r, self.case['positions'][r]) for r in matrix.ROLES])
            del s['players'][1][field]
            observed = matrix.position_observation(s, self.case['positions'])
            with self.subTest(field=field):
                self.assertFalse(matrix.positions_match(observed))
                self.assertEqual(observed['B']['missing_fields'], [field])
    def test_resource_or_actor_changes_fail_even_with_good_probes(self):
        s = dict(phase=1, map=matrix.MAP, guards=[], players=[character('A'), character('B')])
        baseline = matrix.resource_snapshot(s)
        matrix.assert_unchanged(s, baseline)
        for key, value in [('health', 79), ('ammo', 29), ('accepted', 1), ('pawn_name', 'replacement')]:
            changed = copy.deepcopy(s)
            changed['players'][0][key] = value
            with self.subTest(key=key), self.assertRaises(AssertionError):
                matrix.assert_unchanged(changed, baseline)
        s['guards'] = [{'name': 'GUARD_0'}]
        with self.assertRaises(AssertionError):
            matrix.assert_unchanged(s, baseline)

    def test_nonfinite_coordinates_rejected(self):
        with self.assertRaises(AssertionError):
            matrix.vector([0, 0, float('nan')])


class FreshProfileTests(unittest.TestCase):
    def setUp(self):
        self.snapshot = dict(stored_results=[dict(profile='Profile_'+r, outcome=0,
                              progression=dict(experience=0), economy_receipts=0) for r in matrix.ROLES])

    def test_created_unsettled_profiles_are_allowed_and_match_bound_accounts(self):
        actual = matrix.assert_fresh_profiles(self.snapshot, ['Profile_A', 'Profile_B'])
        self.assertEqual(actual, self.snapshot['stored_results'])
        self.assertIsNot(actual, self.snapshot['stored_results'])

    def test_prior_outcome_experience_or_receipts_fail(self):
        for field in ('outcome', 'experience', 'economy_receipts'):
            snapshot = copy.deepcopy(self.snapshot)
            row = snapshot['stored_results'][0]
            (row['progression'] if field == 'experience' else row)[field] = 1
            with self.subTest(field=field), self.assertRaises(AssertionError):
                matrix.assert_fresh_profiles(snapshot)

    def test_missing_fields_do_not_default_to_fresh(self):
        for field in ('outcome', 'progression', 'economy_receipts', 'profile'):
            snapshot = copy.deepcopy(self.snapshot)
            del snapshot['stored_results'][0][field]
            with self.subTest(field=field), self.assertRaises(AssertionError):
                matrix.assert_fresh_profiles(snapshot)

    def test_duplicate_or_foreign_profile_rejected(self):
        self.snapshot['stored_results'][1]['profile'] = 'Profile_A'
        with self.assertRaises(AssertionError):
            matrix.assert_fresh_profiles(self.snapshot)
        self.snapshot['stored_results'][1]['profile'] = 'Foreign'
        with self.assertRaises(AssertionError):
            matrix.assert_fresh_profiles(self.snapshot, ['Profile_A', 'Profile_B'])

    def test_empty_profiles_only_allowed_before_binding_not_as_final_fresh_proof(self):
        self.assertEqual(matrix.assert_fresh_profiles(dict(stored_results=[])), [])
        with self.assertRaises(AssertionError):
            matrix.assert_fresh_profiles(dict(stored_results=[]), ['Profile_A', 'Profile_B'])

class FakeHarness:
    def __init__(self, root, drift=False, replica_delay=0):
        self.root = root
        self.drift = drift
        self.replica_delay = replica_delay
        self.replica_ready_at = 0
        self.clock = 0
        self.ready = set()
        self.aim = False
        self.camera = None
        self.calls = []
        self.players = [character('A'), character('B')]
        self.accounts = {r: dict(bound=True, locked=False, outcome=0, profile='Profile_'+r, revision=1,
                                progression=dict(experience=0), loadout=dict(items=[]),
                                stash=dict(items=[dict(id='Rifle_'+r, definition='AK', quantity=1, slot=-1)])) for r in matrix.ROLES}
        self.report = dict(processes={r: dict(command=['-ExtractionStore='+str(root/'Store'),
                                                     '-ExtractionProfile=unit_'+r]) for r in ('Server','A','B')})
        self.g = dict(ROOT=root, OUT=root, args=SimpleNamespace(guards=0, flight_diagnostics=True, tag='unit'),
                      ROLES=['A','B'], processes={'Server':None,'A':None,'B':None}, rendered_roles=['A'],
                      report=self.report, send=self.send, wait=self.wait, state=self.state,
                      player=self.player, read_snapshot=lambda p:p.read_text(encoding='utf-8'), save=lambda:None)

    def state(self, role):
        active = len(self.ready) == 2
        result = dict(phase=1 if active else 0, map=matrix.MAP if active else '/Game/Maps/Lobby', guards=[],
                      players=copy.deepcopy(self.players), world_time=self.clock,
                      stored_results=[dict(profile=self.accounts[r]['profile'], outcome=0,
                                           progression=dict(experience=0), economy_receipts=0)
                                      for r in matrix.ROLES])
        if role == 'B' and self.clock < self.replica_ready_at:
            remote = next(p for p in result['players'] if p['name'] == 'EX_A')
            for field in ('position', 'falling', 'pawn_name'):
                remote.pop(field)
        if role != 'Server':
            result['account'] = copy.deepcopy(self.accounts[role])
        return result

    def player(self, peer, role):
        return next(p for p in self.state(peer)['players'] if p['name']=='EX_'+role)

    def send(self, role, op, **fields):
        self.calls.append((role,op,copy.deepcopy(fields)))
        if op == 'storage':
            a=self.accounts[role]
            a['loadout']['items']=[dict(id=fields['item'], definition='AK', quantity=1, slot=0)]
            a['revision']+=1
        elif op == 'ready':
            self.ready.add(role)
        elif op == 'fixture':
            self.replica_ready_at = math.inf if self.replica_delay is None else self.clock + self.replica_delay
            for item in fields['players']:
                p=next(p for p in self.players if p['name']==item['name'])
                assert p['health']==item['health'] and p['shield']==item['shield']
                p['position']=list(item['position'])
                if self.drift and item['name']=='EX_B': p['position'][2]-=4
        elif op == 'world_camera':
            self.camera=fields['position']
        elif op == 'aim' and role == 'A':
            self.aim=fields['target']=='EX_B'
        elif op == 'pose':
            (self.root/(role+'-'+fields['name']+'-pose.json')).write_text('{}',encoding='utf-8')
        elif op not in ('walk','inventory','camera','aim'):
            raise AssertionError('Unexpected command '+op)
        return ''

    def wait(self, predicate, message, timeout=45):
        for _ in range(10):
            self.clock+=.6
            if self.aim:
                case=dict(camera=self.camera, positions={r:self.player('Server',r)['position'] for r in matrix.ROLES})
                with (self.root/'A-aim-diagnostics.jsonl').open('a',encoding='utf-8') as f:
                    f.write(json.dumps(probe(case,self.clock))+'\n')
            if predicate(): return
        raise TimeoutError(message)


class MatrixHarnessTests(unittest.TestCase):
    def test_all_four_cases_prepare_normally_capture_raw_and_never_fire(self):
        with tempfile.TemporaryDirectory() as folder:
            h=FakeHarness(Path(folder))
            with patch.object(matrix,'load_history',return_value=history()):
                matrix.run(h.g)
            p=h.report['flight_matrix']
            self.assertEqual(p['result'],'completed_diagnostic_capture')
            self.assertFalse(p['main_route_acceptance'])
            self.assertEqual(len(p['cases']),4)
            self.assertTrue(all(len(c['samples'])>=3 for c in p['cases']))
            self.assertEqual([op for _,op,_ in h.calls].count('storage'),2)
            self.assertEqual([op for _,op,_ in h.calls].count('ready'),2)
            self.assertEqual([op for _,op,_ in h.calls].count('fixture'),4)
            self.assertFalse(any(op in ('pulse','input_key','ammo','guard_spawn') for _,op,_ in h.calls))
            self.assertEqual((h.root/'FlightMatrixRawProbes.jsonl').read_bytes(),
                             (h.root/'A-aim-diagnostics.jsonl').read_bytes())

    def test_delayed_B_observer_replica_is_waited_for_and_all_cases_capture(self):
        with tempfile.TemporaryDirectory() as folder:
            h=FakeHarness(Path(folder), replica_delay=1.8)
            with patch.object(matrix,'load_history',return_value=history()):
                matrix.run(h.g)
            proof=h.report['flight_matrix']
            self.assertEqual(proof['result'],'completed_diagnostic_capture')
            for case in proof['cases']:
                pending=case['replica_pending_observations']
                self.assertTrue(any(p['peer']=='B' and p['role']=='A' for p in pending))
                for sample in case['samples']:
                    self.assertTrue(all(matrix.positions_match(o) for o in sample['observed_positions'].values()))
                    self.assertTrue(sample['observed_positions']['B']['A']['ready'])

    def test_missing_B_observer_replica_times_out_without_skipping_it(self):
        with tempfile.TemporaryDirectory() as folder:
            h=FakeHarness(Path(folder), replica_delay=None)
            with patch.object(matrix,'load_history',return_value=history()), self.assertRaises(TimeoutError):
                matrix.run(h.g)
            proof=json.loads((h.root/'FlightMatrixEvidence.json').read_text(encoding='utf-8'))
            self.assertEqual(proof['result'],'failed')
            self.assertEqual(proof['cases'][0]['samples'],[])
            self.assertFalse(proof['cases'][0]['observed_positions']['B']['A']['ready'])
            self.assertFalse(any(op=='world_camera' for _,op,_ in h.calls))
    def test_position_drift_fails_and_leaves_requested_actual_offsets(self):
        with tempfile.TemporaryDirectory() as folder:
            h=FakeHarness(Path(folder),drift=True)
            with patch.object(matrix,'load_history',return_value=history()), self.assertRaises(TimeoutError):
                matrix.run(h.g)
            p=json.loads((h.root/'FlightMatrixEvidence.json').read_text(encoding='utf-8'))
            self.assertEqual(p['result'],'failed')
            self.assertEqual(p['cases'][0]['observed_positions']['Server']['B']['offset_cm'][2],-4)
            self.assertFalse(any(op=='pose' for _,op,_ in h.calls))


if __name__ == '__main__':
    unittest.main()