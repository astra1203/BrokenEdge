"""Offline regression gates for teammate geometry and extraction evidence."""
import copy
import json
import math
from pathlib import Path
import tempfile
import unittest

from overnight_route_plan import (FloorGraph, edge_avoids_stationary, segment_distance_2d,
                                  edge_avoids_runtime, apply_runtime_avoidance, reserved_path, validate)
from overnight_route_scenarios import (authoritative_raid, assert_closed_square, hold_deadline,
                                       item_owners, ownership_is, sale_is_complete, square_is_open,
                                       settled_after_deadlines, complete_png, path_chunk, complete_path_chunk,
                                       encounter_trigger, validate_encounter, confirm_completed_guard)


class OvernightRouteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.route = json.loads(Path(__file__).with_name('overnight_route.json').read_text(encoding='utf-8'))
        cls.pairs = {(p['a_start'], p['destination']): p for p in cls.route['pair_legs']}

    def test_every_generated_pair_reserves_the_stationary_teammate(self):
        self.assertGreaterEqual(len(self.route['pair_legs']), 22)
        for pair in self.route['pair_legs']:
            self.assertEqual(pair['order'], ['A', 'B'])
            self.assertNotEqual(pair['b_end'], pair['destination'])
            for role, stationary in [('A', pair['b_start']), ('B', pair['destination'])]:
                fixed = self.route['nodes'][stationary]['position']
                points = pair[role]['points']
                self.assertTrue(points)
                for a, b in zip(points, points[1:]):
                    self.assertLessEqual(math.dist(a[:2], b[:2]), 101)
                    self.assertLess(abs(a[2] - b[2]), 80.01)
                    self.assertTrue(edge_avoids_stationary(a, b, fixed), (pair['destination'], role, a, b, fixed))

    def test_reported_reverse_edge_regression_keeps_real_peer_endpoint(self):
        previous = self.pairs[('7F_WestDorm_A1', '7F_WestDorm_B2')]
        following = self.pairs[('7F_WestDorm_B2', '7F_LivingArea_B1')]
        self.assertEqual(previous['b_end'], following['b_start'])
        self.assertLess(math.dist(previous['B']['points'][-1], following['B']['points'][0]), .01)
        # A no longer reverses into the cell where B was arbitrarily truncated.
        parked = previous['B']['points'][-1]
        for a, b in zip(following['A']['points'], following['A']['points'][1:]):
            self.assertTrue(edge_avoids_stationary(a, b, parked))

    def test_reservation_detects_crossing_between_clear_endpoints(self):
        self.assertGreater(math.dist([-200, 0], [0, 0]), 180)
        self.assertGreater(math.dist([200, 0], [0, 0]), 180)
        self.assertFalse(edge_avoids_stationary([-200, 0, 0], [200, 0, 0], [0, 0, 0]))
        self.assertEqual(segment_distance_2d([-200, 0, 0], [200, 0, 0], [0, 0, 0]), 0)

    def test_runtime_zone_checks_crossing_edges_and_clips_to_the_same_floor(self):
        zones = [dict(center=[0, 0, 0], radius_cm=20, floor_z_window_cm=[-10, 10])]
        a, b = [-100, 0, 0], [100, 0, 0]
        self.assertTrue(edge_avoids_runtime(a, a, zones))
        self.assertTrue(edge_avoids_runtime(b, b, zones))
        self.assertFalse(edge_avoids_runtime(a, b, zones), 'Clear endpoints do not prove a clear edge')
        self.assertFalse(edge_avoids_runtime([-100, 0, -20], [100, 0, 20], zones))
        self.assertTrue(edge_avoids_runtime([-100, 0, 1000], [100, 0, 1000], zones))
        self.assertTrue(edge_avoids_runtime([-100, 0, 0], [100, 0, 200], zones),
                        'The XY crossing occurs above the excluded floor')

    def test_runtime_filter_removes_nodes_and_crossing_edges_but_preserves_other_floors(self):
        probes = [dict(location=[x, y, z], clear=True, actor='measured', normal=[0, 0, 1])
                  for x in (0, 100, 200) for y in (0, 100, 200) for z in (0, 1000)]
        graph = FloorGraph(probes)
        zones = [dict(center=[100, 100, 0], radius_cm=20, floor_z_window_cm=[-10, 10]),
                 dict(center=[50, 0, 0], radius_cm=20, floor_z_window_cm=[-10, 10])]
        counts = apply_runtime_avoidance(graph, zones)
        self.assertEqual(counts['removed_nodes'], 1)
        self.assertGreater(counts['blocked_edges'], 0)
        self.assertNotIn((1, 1, 0), graph.nodes)
        self.assertIn((1, 1, 200), graph.nodes)
        path = [p['location'] for p in graph.path([0, 0, 0], [200, 0, 0])]
        self.assertGreater(len(path), 3, 'The planner must genuinely walk around the excluded area')
        for a, b in zip(path, path[1:]):
            self.assertTrue(edge_avoids_runtime(a, b, zones))
        upper = graph.path([0, 0, 1000], [200, 0, 1000])
        self.assertEqual(len(upper), 3)

    def test_cached_disconnected_peer_component_does_not_reject_a_separate_clear_corridor(self):
        probes = [dict(location=[x, y, 0], clear=True, actor='measured', normal=[0, 0, 1])
                  for x in range(0, 701, 100) for y in (0, 1000)]
        graph = FloorGraph(probes)
        stationary = [350, 0, 0]
        for start, end in [([0, 0, 0], [700, 0, 0]), ([700, 0, 0], [0, 0, 0])]:
            with self.assertRaises(ValueError):
                reserved_path(graph, start, end, stationary)
        clear = reserved_path(graph, [0, 1000, 0], [700, 1000, 0], stationary)
        self.assertEqual(len(clear), 8)
        self.assertEqual(clear, reserved_path(graph, [0, 1000, 0], [700, 1000, 0], stationary))
        for a, b in zip(clear, clear[1:]):
            self.assertTrue(edge_avoids_stationary(a['location'], b['location'], stationary))

    def test_all_82_generated_pairs_avoid_the_recorded_runtime_blocker(self):
        self.assertEqual(len(self.route['pair_legs']), 82)
        zones = self.route['runtime_avoidance']
        self.assertEqual(len(zones), 2)
        self.assertEqual(zones[0]['evidence']['run'], 'overnight-route01')
        self.assertEqual(zones[1]['evidence']['run'], 'overnight-route-flight01')
        for zone in zones:
            self.assertEqual(zone['floor_z_window_cm'], [100, 280])
            for expected in zone['evidence']['sha256'].values():
                self.assertEqual(len(expected), 64)
        self.assertEqual(validate(self.route), [])
        for pair in self.route['pair_legs']:
            for role in ('A', 'B'):
                points = pair[role]['points']
                for point in points:
                    self.assertTrue(edge_avoids_runtime(point, point, zones))
                for a, b in zip(points, points[1:]):
                    self.assertTrue(edge_avoids_runtime(a, b, zones), (pair['a_start'], pair['destination'], role, a, b))
        failed_edge = [[-19300, -19800, 191.9895], [-19300, -19700, 184]]
        self.assertFalse(edge_avoids_runtime(*failed_edge, zones))

    def test_living_area_guard_leg_no_longer_walks_through_the_live_gear_crate(self):
        zone = next(z for z in self.route['runtime_avoidance'] if z.get('source_point') == '7F_LivingArea_A2')
        old_edge = [[-16300, -17900, 195.6411], [-16300, -18000, 197.984]]
        self.assertFalse(edge_avoids_runtime(*old_edge, [zone]))
        self.assertTrue(edge_avoids_runtime([-16300, -17900, -200], [-16300, -18000, -200], [zone]),
                        'Do not remove the separate lower floor because a crate exists above it')
        pair = self.pairs[('7F_LivingArea_B2', 'ExtractionPatrol_7F_WestDorm')]
        for role in ('A', 'B'):
            points = pair[role]['points']
            self.assertGreater(min(math.dist(p[:2], zone['center'][:2]) for p in points
                                   if 100 <= p[2] <= 280), zone['radius_cm'])
            for a, b in zip(points, points[1:]):
                self.assertTrue(edge_avoids_runtime(a, b, [zone]))

    def test_generated_peer_parking_does_not_occupy_an_independent_candidate_crate(self):
        plan = json.loads((Path(__file__).resolve().parents[2]
                           / 'Artifacts/Extraction/RestorationEvidence/Phase7FGameplay/Plan.json').read_text(encoding='utf-8'))
        for node in self.route['nodes'].values():
            if node['kind'] != 'peer_parking':
                continue
            for box in plan['containers']:
                p, q = node['position'], box['location']
                self.assertTrue(abs(p[2]-q[2]) >= 200 or math.dist(p[:2], q[:2]) >= 180,
                                (node['id'], 'would park on candidate crate', box['id']))

    def test_closed_exit_rejects_early_progress_or_settlement(self):
        valid = dict(life=0, extract_at=0, extract_exit_id='OfficialExitSquare', extract_blocked_reason='900 seconds')
        assert_closed_square(valid, 0, 'OfficialExitSquare')
        for altered, outcome in [(valid | {'extract_at': 905}, 0), (valid, 1),
                                 (valid | {'extract_blocked_reason': ''}, 0)]:
            with self.assertRaises(AssertionError):
                assert_closed_square(altered, outcome, 'OfficialExitSquare')

    def test_hold_requires_correct_exit_and_full_duration(self):
        valid = dict(extract_exit_id='OfficialExitSquare', extract_at=910)
        self.assertEqual(hold_deadline(valid, 'OfficialExitSquare', 900, 10), 910)
        for altered in [valid | {'extract_at': 909}, valid | {'extract_exit_id': 'Other'}]:
            with self.assertRaises(AssertionError):
                hold_deadline(altered, 'OfficialExitSquare', 900, 10)

    @staticmethod
    def exit_snapshot(now, deadline=0, outcome=0):
        return dict(world_time=now,
                    exits=[dict(id='OfficialExitSquare', available=now >= 900)],
                    players=[dict(name='EX_' + role, life=0 if outcome == 0 else 2, health=100,
                                  extract_at=deadline, extract_exit_id='OfficialExitSquare',
                                  extract_blocked_reason='opens later' if now < 900 else '',
                                  account=dict(outcome=outcome)) for role in ('A', 'B')])

    def test_opening_boundary_reads_time_and_players_from_one_snapshot(self):
        snapshots = [self.exit_snapshot(899.99), self.exit_snapshot(900.01, deadline=910.01)]
        reads = []

        def read():
            reads.append(len(reads))
            return snapshots[len(reads)-1]

        self.assertFalse(square_is_open(read, ['A', 'B'], 'OfficialExitSquare', 900))
        self.assertEqual(len(reads), 1, 'An observation must not refresh the clock and players separately')
        self.assertTrue(square_is_open(read, ['A', 'B'], 'OfficialExitSquare', 900))
        self.assertEqual(len(reads), 2)
        early = self.exit_snapshot(899.99, deadline=909.99)
        with self.assertRaises(AssertionError):
            square_is_open(lambda: early, ['A', 'B'], 'OfficialExitSquare', 900)

    def test_settlement_boundary_reads_once_and_still_rejects_early_outcome(self):
        holds = dict(A=910, B=910)
        snapshots = [self.exit_snapshot(909.98, deadline=910), self.exit_snapshot(910.01, outcome=1)]
        reads = []

        def read():
            reads.append(len(reads))
            return snapshots[len(reads)-1]

        self.assertFalse(settled_after_deadlines(read, ['A', 'B'], 'OfficialExitSquare', holds))
        self.assertEqual(len(reads), 1)
        self.assertTrue(settled_after_deadlines(read, ['A', 'B'], 'OfficialExitSquare', holds))
        self.assertEqual(len(reads), 2)
        early = self.exit_snapshot(909.98, outcome=1)
        with self.assertRaises(AssertionError):
            settled_after_deadlines(lambda: early, ['A', 'B'], 'OfficialExitSquare', holds)

    def test_capture_requires_png_signature_and_complete_final_chunk(self):
        data = b'\x89PNG\r\n\x1a\n' + b'x' * 128 + b'\x00\x00\x00\x00IEND\xae\x42\x60\x82'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.png'
            self.assertFalse(complete_png(path))
            path.write_bytes(data[:-1])
            self.assertTrue(path.is_file(), 'A partially written screenshot already exists')
            self.assertFalse(complete_png(path))
            path.write_bytes(data)
            self.assertTrue(complete_png(path))
            path.write_bytes(b'invalid!' + data[8:])
            self.assertFalse(complete_png(path))

    def test_short_chunks_resume_original_points_after_driver_cursor_reset(self):
        points = [[i * 100, 0, 0] for i in range(11)]
        completed, visited, offsets = 0, [], []
        while completed < len(points):
            chunk = path_chunk(points, completed)
            self.assertLessEqual(len(chunk['points']), 4)
            actor = dict(position=[*chunk['points'][-1][:2], 92], capsule_half=92, falling=False)
            movement = dict(walk_count=len(chunk['points']), walk_index=len(chunk['points']))
            previous = completed
            completed = complete_path_chunk(chunk, completed, movement, actor)
            offsets.append(completed)
            visited.extend(chunk['points'])
            # Combat clears the driver's local WalkIndex. It must not clear the
            # Python global cursor or count the same original points a second time.
            with self.assertRaises(AssertionError):
                complete_path_chunk(chunk, completed, dict(walk_count=0, walk_index=0), actor)
            if completed < len(points):
                resumed = path_chunk(points, completed)
                self.assertEqual(resumed['start'], completed)
                self.assertEqual(resumed['points'][0], points[completed])
                self.assertGreater(resumed['start'], previous)
        self.assertEqual(offsets, [4, 8, 11])
        self.assertEqual(visited, points)
        self.assertIsNone(path_chunk(points, completed))

    def test_chunk_cannot_advance_for_skipped_points_or_missing_ground(self):
        points = [[i * 100, 0, 0] for i in range(8)]
        chunk = path_chunk(points, 0)
        actor = dict(position=[300, 0, 92], capsule_half=92, falling=False)
        movement = dict(walk_count=4, walk_index=4)
        for wrong_chunk, wrong_completed, wrong_movement, wrong_actor in [
            (path_chunk(points, 4), 0, movement, actor),
            (chunk, 0, movement | {'walk_index': 3}, actor),
            (chunk, 0, movement | {'walk_count': 8}, actor),
            (chunk, 0, movement, actor | {'falling': True}),
            (chunk, 0, movement, actor | {'position': [300, 0, 400]}),
        ]:
            with self.assertRaises(AssertionError):
                complete_path_chunk(wrong_chunk, wrong_completed, wrong_movement, wrong_actor)

    @staticmethod
    def encounter_snapshot():
        return dict(world_time=100, players=[dict(name='EX_' + role, pawn_name='Pawn_' + role,
                    life=0, health=100, accepted=10, guard_kills=0) for role in ('A', 'B')],
                    guards=[dict(name='GUARD_7', route='Dorm', target='Pawn_B', state=1,
                                 health=100, dead=False, shots=0, line_of_sight_to_A=True)])

    def test_natural_encounter_uses_actual_target_and_authored_route(self):
        snapshot = self.encounter_snapshot()
        trigger = encounter_trigger(snapshot, 'Dorm')
        self.assertEqual(trigger['shooter'], 'B')
        self.assertEqual(trigger['target'], 'Pawn_B')
        self.assertIsNone(encounter_trigger(snapshot, 'Different route'))
        for fields in [dict(target='None'), dict(target='EX_B'), dict(dead=True), dict(route='Other')]:
            changed = copy.deepcopy(snapshot)
            changed['guards'][0].update(fields)
            self.assertIsNone(encounter_trigger(changed, 'Dorm'))
        changed = copy.deepcopy(snapshot)
        changed['players'][1]['life'] = 1
        self.assertIsNone(encounter_trigger(changed, 'Dorm'))

    def test_guard_death_requires_real_shots_damage_and_shooter_credit(self):
        before = self.encounter_snapshot()
        after = copy.deepcopy(before)
        after['guards'][0].update(dead=True, health=0)
        after['players'][1].update(accepted=15, guard_kills=1)
        self.assertEqual(validate_encounter(before, after, 'B', 'GUARD_7')['accepted_shots'], 5)
        for fields in [dict(accepted=10), dict(guard_kills=0), dict(guard_kills=2)]:
            changed = copy.deepcopy(after)
            changed['players'][1].update(fields)
            with self.assertRaises(AssertionError):
                validate_encounter(before, changed, 'B', 'GUARD_7')
        changed = copy.deepcopy(after)
        changed['guards'][0]['health'] = 100
        with self.assertRaises(AssertionError):
            validate_encounter(before, changed, 'B', 'GUARD_7')
        with self.assertRaises(AssertionError):
            validate_encounter(after, after, 'B', 'GUARD_7')

    def completed_guard_proof(self):
        before = self.encounter_snapshot()
        after = copy.deepcopy(before)
        after['guards'][0].update(dead=True, health=0)
        after['players'][1].update(accepted=15, guard_kills=1)
        evidence = validate_encounter(before, after, 'B', 'GUARD_7')
        event = dict(evidence, result='passed', trigger=encounter_trigger(before, 'Dorm'))
        proof = dict(authored_guard=dict(name='GUARD_7', route='Dorm'),
                     combat=dict(evidence, encounter_index=0), encounters=[event])
        return after, proof

    def test_early_defeated_guard_may_be_normally_reclaimed(self):
        snapshot, proof = self.completed_guard_proof()
        self.assertEqual(confirm_completed_guard(snapshot, 'Dorm', proof), 'present_dead')
        snapshot['guards'] = []
        self.assertEqual(confirm_completed_guard(snapshot, 'Dorm', proof), 'reclaimed_after_verified_defeat')

    def test_missing_guard_without_complete_combat_proof_is_rejected(self):
        snapshot, proof = self.completed_guard_proof()
        snapshot['guards'] = []
        for change in ('empty', 'no_combat', 'no_encounter', 'not_passed', 'no_death', 'no_shots', 'no_credit'):
            bad = copy.deepcopy(proof)
            if change == 'empty': bad = {}
            elif change == 'no_combat': bad.pop('combat')
            elif change == 'no_encounter': bad['encounters'] = []
            elif change == 'not_passed': bad['encounters'][0]['result'] = 'running'
            else:
                field, value = {'no_death': ('guard_dead_after', False), 'no_shots': ('accepted_shots', 0),
                                'no_credit': ('kills_added', 0)}[change]
                bad['combat'][field] = bad['encounters'][0][field] = value
            with self.subTest(change=change), self.assertRaises(AssertionError):
                confirm_completed_guard(snapshot, 'Dorm', bad)

    def test_reclaimed_guard_proof_cannot_use_another_identity(self):
        snapshot, proof = self.completed_guard_proof()
        snapshot['guards'] = []
        for change in ('initial', 'summary', 'trigger', 'replacement', 'credit_lost'):
            bad, current = copy.deepcopy(proof), copy.deepcopy(snapshot)
            if change == 'initial': bad['authored_guard']['name'] = 'GUARD_OTHER'
            elif change == 'summary': bad['combat']['guard'] = bad['encounters'][0]['guard'] = 'GUARD_OTHER'
            elif change == 'trigger': bad['encounters'][0]['trigger']['guard'] = 'GUARD_OTHER'
            elif change == 'replacement': current['guards'] = [dict(name='GUARD_OTHER', route='Dorm', dead=True)]
            else: current['players'][1]['guard_kills'] = 0
            with self.subTest(change=change), self.assertRaises(AssertionError):
                confirm_completed_guard(current, 'Dorm', bad)

    def test_raid_identity_must_exist_be_well_formed_and_nonzero(self):
        for value in [None, '', 'not-a-guid', '0' * 32]:
            with self.assertRaises(AssertionError):
                authoritative_raid(dict(extraction_raid=value))
        self.assertEqual(authoritative_raid(dict(extraction_raid='00000000000000000000000000000001')),
                         '00000000-0000-0000-0000-000000000001')

    @staticmethod
    def ownership_snapshot():
        account = dict(revision=7, progression=dict(credits=1000),
                       stash=dict(items=[]), loadout=dict(items=[]), overflow=dict(items=[]))
        return dict(players=[dict(name='EX_A', account=copy.deepcopy(account), inventory=dict(items=[])),
                             dict(name='EX_B', account=copy.deepcopy(account), inventory=dict(items=[]))],
                    loot=[dict(id='box', contents=dict(items=[]))],
                    guards=[dict(name='guard', inventory=dict(items=[]))])

    def test_unique_ownership_rejects_peer_loot_guard_and_attachment_duplicates(self):
        snapshot = self.ownership_snapshot()
        item = dict(id='looted', definition='Tools', quantity=1, slot=-1)
        snapshot['players'][0]['account']['stash']['items'].append(item)
        self.assertTrue(ownership_is(snapshot, 'looted', 'player:EX_A:stash', 1))
        # A matching GUID in any other live owner must invalidate the claim,
        # including an overflow item, a teammate inventory or a nested part.
        for target in ('peer_stash', 'peer_overflow', 'peer_inventory', 'loot', 'guard', 'attachment'):
            changed = copy.deepcopy(snapshot)
            containers = dict(peer_stash=changed['players'][1]['account']['stash']['items'],
                              peer_overflow=changed['players'][1]['account']['overflow']['items'],
                              peer_inventory=changed['players'][1]['inventory']['items'],
                              loot=changed['loot'][0]['contents']['items'],
                              guard=changed['guards'][0]['inventory']['items'])
            if target == 'attachment':
                changed['players'][1]['inventory']['items'].append(dict(id='gun', attachments=[item]))
            else:
                containers[target].append(item)
            self.assertEqual(len(item_owners(changed, 'looted')), 2, target)
            self.assertFalse(ownership_is(changed, 'looted', 'player:EX_A:stash', 1), target)
        self.assertFalse(ownership_is(snapshot, 'looted', 'player:EX_A:stash', 2))
        self.assertFalse(ownership_is(snapshot, 'looted', 'player:EX_B:stash', 1))

    def test_original_weapon_requires_exact_identity_quantity_and_primary_slot(self):
        snapshot = self.ownership_snapshot()
        weapon = dict(id='original-gun', definition='AK', quantity=1, slot=0)
        snapshot['players'][0]['inventory']['items'].append(weapon)
        self.assertTrue(ownership_is(snapshot, 'original-gun', 'player:EX_A:inventory', 1, 0))
        for field, value in [('id', 'replacement-gun'), ('slot', 1), ('quantity', 2)]:
            changed = copy.deepcopy(snapshot)
            changed['players'][0]['inventory']['items'][0][field] = value
            self.assertFalse(ownership_is(changed, 'original-gun', 'player:EX_A:inventory', 1, 0))

    def test_sale_requires_exact_credit_delta_new_revision_and_no_remaining_owner(self):
        snapshot = self.ownership_snapshot()
        account = snapshot['players'][0]['account']
        account['revision'] = 8
        account['progression']['credits'] = 1200
        self.assertTrue(sale_is_complete(snapshot, 'A', 'sold', 1000, 200, 7))
        for credits in (1000, 1100, 1400):
            changed = copy.deepcopy(snapshot)
            changed['players'][0]['account']['progression']['credits'] = credits
            self.assertFalse(sale_is_complete(changed, 'A', 'sold', 1000, 200, 7))
        changed = copy.deepcopy(snapshot)
        changed['players'][0]['account']['revision'] = 7
        self.assertFalse(sale_is_complete(changed, 'A', 'sold', 1000, 200, 7))
        changed = copy.deepcopy(snapshot)
        changed['players'][1]['account']['overflow']['items'].append(dict(id='sold', quantity=1))
        self.assertFalse(sale_is_complete(changed, 'A', 'sold', 1000, 200, 7))
        for quote in (None, 0, -1):
            with self.assertRaises(AssertionError):
                sale_is_complete(snapshot, 'A', 'sold', 1000, quote, 7)


if __name__ == '__main__':
    unittest.main()
