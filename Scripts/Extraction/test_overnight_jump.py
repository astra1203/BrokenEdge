"""Offline boundary checks; these do not claim the bridge jump works in UE."""
import unittest

from overnight_route_scenarios import (
    BRIDGE_POINTS, LowLedgeJump, at_ground_point, complete_path_chunk, ledge_return_points, path_chunk,
    project_edge, signed_edge_fraction,
)


class OvernightJumpTests(unittest.TestCase):
    @staticmethod
    def participant(x=2611.078, y=-18600.14, feet=379.552, falling=False):
        return dict(position=[x, y, feet + 92], capsule_half=92, life=0, health=100,
                    falling=falling, traversal=0, disabled=False, crouched=False,
                    inventory_open=False, menu_open=False)

    @staticmethod
    def route():
        # Consecutive actual floor samples from the frozen route, no synthetic
        # landing/return waypoint is introduced by recovery.
        return [[2400., -18600., 274.5848], [2500., -18600., 326.1215],
                [2600., -18600., 359.966], [2700., -18600., 395.0148],
                [2800., -18600., 395.0148], [2900., -18600., 395.0198],
                [3000., -18600., 395.0478], [3100., -18600., 411.1523],
                [3200., -18600., 443.6607], [3300., -18600., 493.2117],
                [3300., -18500., 510.3187]]

    def jump(self):
        chunk = dict(start=252, end=256, points=self.route()[:4])
        return LowLedgeJump(chunk), dict(walk_count=4, walk_index=3, error='')

    def triggered(self):
        recovery, movement = self.jump()
        self.assertFalse(recovery.should_jump(100., movement, self.participant()))
        self.assertTrue(recovery.should_jump(102., movement, self.participant()))
        return recovery, movement

    def test_requires_two_seconds_of_new_authority_time_not_poll_count(self):
        recovery, movement = self.jump()
        self.assertFalse(recovery.should_jump(100, movement, self.participant()))
        for _ in range(30):
            self.assertFalse(recovery.should_jump(100, movement, self.participant()))
        self.assertFalse(recovery.should_jump(99, movement, self.participant()))
        self.assertFalse(recovery.should_jump(101.99, movement, self.participant()))
        self.assertTrue(recovery.should_jump(102, movement, self.participant()))
        self.assertEqual(recovery.event['original_global_index'], 255)

    def test_movement_or_another_current_target_restarts_stall_observation(self):
        recovery, movement = self.jump()
        recovery.should_jump(100, movement, self.participant())
        self.assertFalse(recovery.should_jump(101.9, movement, self.participant(x=2617)))
        self.assertFalse(recovery.should_jump(102, movement, self.participant(x=2617)))
        self.assertFalse(recovery.should_jump(103, dict(movement, walk_index=2), self.participant(x=2617)))
        self.assertFalse(recovery.should_jump(104, movement, self.participant(x=2617)))
        self.assertFalse(recovery.should_jump(105.99, movement, self.participant(x=2617)))
        self.assertTrue(recovery.should_jump(106, movement, self.participant(x=2617)))

    def test_only_the_observed_position_layer_target_and_living_input_state_qualify(self):
        recovery, movement = self.jump()
        self.assertTrue(recovery.eligible(movement, self.participant()))
        for changed in (self.participant(x=2400), self.participant(x=2680),
                        self.participant(y=-18530), self.participant(feet=450),
                        self.participant(feet=290), self.participant(falling=True)):
            self.assertFalse(recovery.eligible(movement, changed))
        for field, value in (('life', 1), ('health', 0), ('traversal', 1), ('disabled', True),
                             ('crouched', True), ('inventory_open', True), ('menu_open', True)):
            changed = self.participant()
            changed[field] = value
            self.assertFalse(recovery.eligible(movement, changed), field)
        for changed in (dict(movement, error='Walk stalled'), dict(movement, walk_index=4),
                        dict(movement, walk_index=2), dict(movement, walk_count=3)):
            self.assertFalse(recovery.eligible(changed, self.participant()))
        recovery.chunk['points'][3] = [2700, -18600, 400]
        self.assertFalse(recovery.eligible(movement, self.participant()))

    def test_b_uses_the_same_physical_target_at_its_own_original_index(self):
        recovery = LowLedgeJump(dict(start=260, end=264, points=self.route()[3:7]))
        movement = dict(walk_count=4, walk_index=0)
        self.assertFalse(recovery.should_jump(200, movement, self.participant()))
        self.assertTrue(recovery.should_jump(202, movement, self.participant()))
        self.assertEqual(recovery.event['original_global_index'], 260)
        self.assertEqual(recovery.event['original_local_index'], 0)

    def test_another_chunk_requires_previous_landing_and_actual_progress(self):
        recovery, movement = self.triggered()
        self.assertFalse(recovery.should_jump(105, movement, self.participant()))
        another = LowLedgeJump(recovery.chunk, recovery.history)
        self.assertFalse(another.should_jump(110, movement, self.participant()))
        self.assertFalse(another.should_jump(115, movement, self.participant()))
        self.assertIsNone(another.event)
        recovery.observe_air(102.2, self.participant(x=2620, feet=460, falling=True))
        recovery.observe_air(102.8, self.participant())
        self.assertFalse(another.should_jump(116, movement, self.participant()))
        with self.assertRaisesRegex(AssertionError, 'same unadvanced obstruction'):
            another.should_jump(118, movement, self.participant())

    def test_second_real_riser_before_the_same_original_target_allows_a_new_jump(self):
        chunk = dict(start=260, end=264, points=BRIDGE_POINTS[10:14])
        recovery = LowLedgeJump(chunk)
        movement = dict(walk_count=4, walk_index=0)
        first = self.participant(x=3110, y=-18627, feet=413.28)
        self.assertFalse(recovery.should_jump(100, movement, first))
        self.assertTrue(recovery.should_jump(102, movement, first))
        recovery.observe_air(102.2, self.participant(x=3120, y=-18620, feet=470, falling=True))
        second = self.participant(x=3140, y=-18610, feet=429)
        recovery.observe_air(102.8, second)
        self.assertFalse(recovery.should_jump(102.8, movement, second))
        self.assertTrue(recovery.should_jump(104.8, movement, second))
        self.assertEqual([e['original_global_index'] for e in recovery.events], [260, 260])
        self.assertEqual([e['attempt'] for e in recovery.events], [1, 2])
        self.assertIsNotNone(recovery.events[0]['landing'])
        self.assertIsNone(recovery.events[1]['landing'])

    def test_reaching_another_target_does_not_reuse_the_old_stall_clock(self):
        recovery = LowLedgeJump(dict(start=260, end=264, points=BRIDGE_POINTS[10:14]))
        movement = dict(walk_count=4, walk_index=0)
        self.assertFalse(recovery.should_jump(100, movement, self.participant(x=3110, feet=413)))
        moved = dict(movement, walk_index=1)
        participant = self.participant(x=3210, feet=450)
        self.assertFalse(recovery.should_jump(101.9, moved, participant))
        self.assertFalse(recovery.should_jump(102, moved, participant))
        self.assertTrue(recovery.should_jump(104, moved, participant))
        self.assertEqual(recovery.event['original_global_index'], 261)

    def test_budget_is_per_role_and_shared_across_chunks_with_36_attempt_limit(self):
        history = []
        for attempt in range(36):
            target, fraction = attempt // 2 + 1, .2 if attempt % 2 == 0 else .5
            chunk = dict(start=1000 + target, end=1001 + target, points=[BRIDGE_POINTS[target]])
            recovery = LowLedgeJump(chunk, history)
            left, right = BRIDGE_POINTS[target-1:target+1]
            position = [left[i] + fraction * (right[i] - left[i]) for i in range(3)]
            participant = self.participant(x=position[0], y=position[1], feet=position[2]+2)
            movement = dict(walk_count=1, walk_index=0)
            now = 100 + 3 * attempt
            self.assertFalse(recovery.should_jump(now, movement, participant))
            self.assertTrue(recovery.should_jump(now+2, movement, participant))
            recovery.observe_air(now+2.1, dict(participant, falling=True))
            recovery.observe_air(now+2.5, participant)
        self.assertEqual(len(history), 36)
        extra = LowLedgeJump(dict(start=2000, end=2001, points=[BRIDGE_POINTS[19]]), history)
        participant = self.participant(x=3820, y=-18400, feet=737)
        self.assertFalse(extra.should_jump(210, dict(walk_count=1, walk_index=0), participant))
        with self.assertRaisesRegex(AssertionError, '36 Jump limit'):
            extra.should_jump(212, dict(walk_count=1, walk_index=0), participant)
        other_role = LowLedgeJump(extra.chunk, [])
        self.assertFalse(other_role.should_jump(210, dict(walk_count=1, walk_index=0), participant))
        self.assertTrue(other_role.should_jump(212, dict(walk_count=1, walk_index=0), participant))

    def test_180_second_budget_includes_waiting_for_air_ground_and_return(self):
        recovery, movement = self.triggered()
        recovery.observe_air(102.2, self.participant(falling=True))
        recovery.observe_air(102.8, self.participant(x=2660, feet=397))
        recovery.check_budget(282)
        with self.assertRaisesRegex(AssertionError, '180 second budget'):
            recovery.check_budget(282.01)
        another = LowLedgeJump(dict(start=500, end=501, points=[BRIDGE_POINTS[10]]), recovery.history)
        with self.assertRaisesRegex(AssertionError, '180 second budget'):
            another.should_jump(283, dict(walk_count=1, walk_index=0), self.participant(x=3110, feet=413))

    def test_input_acceptance_and_completed_xy_cursor_do_not_prove_air_and_landing(self):
        recovery, _ = self.triggered()
        recovery.observe_air(102.1, self.participant())
        self.assertIsNone(recovery.event['falling'])
        self.assertIsNone(recovery.event['landing'])
        recovery.observe_air(102.2, self.participant(x=2710, feet=470, falling=True))
        self.assertIsNotNone(recovery.event['falling'])
        self.assertIsNone(recovery.event['landing'])
        recovery.observe_air(102.2, self.participant(x=2710))
        self.assertIsNone(recovery.event['landing'])
        recovery.observe_air(102.8, self.participant(x=3070, feet=406))
        self.assertEqual(recovery.event['landing']['world_time'], 102.8)

    def test_no_observed_air_fails_instead_of_claiming_success(self):
        recovery, _ = self.triggered()
        recovery.observe_air(105, self.participant())
        with self.assertRaisesRegex(AssertionError, 'observed authoritative falling'):
            recovery.observe_air(105.01, self.participant())
        late, _ = self.triggered()
        with self.assertRaisesRegex(AssertionError, 'within 3 seconds'):
            late.observe_air(105.01, self.participant(falling=True))

    def test_grounded_runout_returns_only_existing_points_in_reverse(self):
        points = self.route()
        chunk = path_chunk(points, 0)
        landing = self.participant(x=3150, feet=427)
        result = ledge_return_points(points, chunk, landing)
        self.assertEqual(result, list(reversed(points[3:8])))
        self.assertTrue(all(point in points for point in result))
        self.assertEqual(chunk, path_chunk(points, 0))
        # The real B slice ends at 3000; its shorter return still ends there.
        b_chunk = path_chunk(points, 3)
        self.assertEqual(ledge_return_points(points, b_chunk, landing), list(reversed(points[6:8])))

    def test_runout_rejects_air_another_layer_and_unmeasured_shortcuts(self):
        points, chunk = self.route(), path_chunk(self.route(), 0)
        for landing in (self.participant(x=3100, falling=True),
                        self.participant(x=3100, feet=700), self.participant(x=3500, feet=592),
                        self.participant(x=3100, y=-18530, feet=411),
                        self.participant(x=2100, feet=130)):
            with self.assertRaises(AssertionError):
                ledge_return_points(points, chunk, landing)

    def test_return_follows_each_original_corner_and_can_walk_forward_to_endpoint(self):
        chunk = path_chunk(BRIDGE_POINTS, 8)  # endpoint [3300, -18600]
        result = ledge_return_points(BRIDGE_POINTS, chunk,
                                    self.participant(x=3450, y=-18500, feet=568))
        self.assertEqual(result, list(reversed(BRIDGE_POINTS[11:14])))
        self.assertIn([3300., -18500., 510.3187], result)
        self.assertEqual(result[-1], chunk['points'][-1])
        ahead = path_chunk(BRIDGE_POINTS, 10)  # endpoint [3400, -18500]
        result = ledge_return_points(BRIDGE_POINTS, ahead,
                                    self.participant(x=3200, y=-18600, feet=443.66))
        self.assertEqual(result[-3:], BRIDGE_POINTS[11:14])
        self.assertEqual(ahead['start'], 10)

    def test_scope_matches_frozen_both_player_geometry_not_a_global_indices(self):
        import json
        from pathlib import Path
        route = json.loads(Path(__file__).with_name('overnight_route.json').read_text(encoding='utf-8'))
        pair = next(p for p in route['pair_legs'] if p['a_start'] == 'ExtractionPatrol_7F_WestDorm')
        self.assertEqual(pair['A']['points'][250:280], BRIDGE_POINTS)
        self.assertEqual(pair['B']['points'][255:285], BRIDGE_POINTS)
        for start in (260, 265, 999):
            recovery = LowLedgeJump(dict(start=start, end=start+1, points=[BRIDGE_POINTS[10]]))
            self.assertTrue(recovery.eligible(dict(walk_count=1, walk_index=0), self.participant(x=3110, feet=413)))
        recovery = LowLedgeJump(dict(start=999, end=1000, points=[[4700, -18100, 907.]]))
        self.assertFalse(recovery.eligible(dict(walk_count=1, walk_index=0), self.participant(x=4610, y=-18100, feet=909)))

    def test_route06_corner_uses_frozen_observed_start_without_accepting_endpoint(self):
        recovery = LowLedgeJump(dict(start=260, end=264, points=BRIDGE_POINTS[10:14]))
        movement = dict(walk_count=4, walk_index=2)
        start = self.participant(x=3260.3378485, y=-18606.0691498, feet=479.3304474)
        stopped = self.participant(x=3234.116, y=-18520.101, feet=479.3304474)
        self.assertFalse(recovery.eligible(movement, stopped))
        self.assertFalse(recovery.should_jump(324.7586297, movement, start))
        scope = recovery.movement_scope(movement, stopped)
        self.assertAlmostEqual(scope['measured_edge_distance_cm'], 65.884, places=3)
        self.assertLess(scope['observed_input_edge_distance_cm'], 65)
        self.assertTrue(scope['allowed'])
        self.assertFalse(at_ground_point(stopped, BRIDGE_POINTS[12]))
        self.assertFalse(recovery.should_jump(326, movement, stopped))
        self.assertTrue(recovery.should_jump(328, movement, stopped))
        observed = recovery.event['trigger']['movement_scope']['observed_start']
        self.assertEqual(observed['position'], start['position'])
        self.assertEqual(observed['world_time'], 324.7586297)
        self.assertEqual(recovery.event['original_global_index'], 262)

    def test_corner_corridor_cannot_recenter_or_admit_farther_off_route_and_wrong_floor(self):
        recovery = LowLedgeJump(dict(start=260, end=264, points=BRIDGE_POINTS[10:14]))
        movement = dict(walk_count=4, walk_index=2)
        start = self.participant(x=3260.3378485, y=-18606.0691498, feet=479.33)
        recovery.should_jump(100, movement, start)
        saved = dict(recovery.movement_start)
        farther = self.participant(x=3200, y=-18520.101, feet=479.33)
        self.assertFalse(recovery.should_jump(102, movement, farther))
        self.assertFalse(recovery.should_jump(104, movement, farther))
        self.assertEqual(recovery.movement_start, saved)
        self.assertFalse(recovery.eligible(movement, farther))
        self.assertFalse(recovery.eligible(movement, self.participant(x=3234.116, y=-18520.101, feet=600)))
        arrived = self.participant(x=3290, y=-18500, feet=510.319)
        self.assertFalse(recovery.eligible(movement, arrived))

    def test_corner_anchor_requires_real_ground_within_previous_original_45cm(self):
        movement = dict(walk_count=4, walk_index=2)
        for invalid in (self.participant(x=3250, y=-18606, feet=479),
                        self.participant(x=3260, y=-18606, feet=479, falling=True),
                        self.participant(x=3260, y=-18606, feet=400)):
            recovery = LowLedgeJump(dict(start=260, end=264, points=BRIDGE_POINTS[10:14]))
            recovery.should_jump(100, movement, invalid)
            self.assertIsNone(recovery.movement_start)
            self.assertFalse(recovery.eligible(movement, self.participant(x=3234.116, y=-18520.101, feet=479.33)))

    def test_changing_original_target_discards_old_observed_input_corridor(self):
        recovery = LowLedgeJump(dict(start=260, end=264, points=BRIDGE_POINTS[10:14]))
        recovery.should_jump(100, dict(walk_count=4, walk_index=2),
                             self.participant(x=3260.3378485, y=-18606.0691498, feet=479.33))
        self.assertIsNotNone(recovery.movement_start)
        stopped = self.participant(x=3234.116, y=-18520.101, feet=479.33)
        self.assertFalse(recovery.should_jump(101, dict(walk_count=4, walk_index=3), stopped))
        self.assertIsNone(recovery.movement_start)

    def route07_turn(self):
        recovery = LowLedgeJump(dict(start=260, end=264, points=BRIDGE_POINTS[10:14]))
        movement = dict(walk_count=4, walk_index=2)
        recovery.should_jump(323.1147892, movement,
                             self.participant(x=3261.3997272, y=-18609.6695666, feet=479.3303704))
        trigger = self.participant(x=3234.0848117, y=-18520.1178754, feet=479.3303704)
        self.assertFalse(recovery.should_jump(324.0825062, movement, trigger))
        self.assertTrue(recovery.should_jump(326.2523276, movement, trigger))
        recovery.observe_air(326.9, self.participant(x=3240, y=-18518, feet=545, falling=True))
        landing = self.participant(x=3270.8555433, y=-18511.4187508, feet=495.3614187)
        recovery.observe_air(327.6536952, landing)
        return recovery, dict(walk_count=4, walk_index=3), landing

    def test_route07_turn_accepts_real_completed_original_point_despite_projection(self):
        recovery, movement, landing = self.route07_turn()
        self.assertFalse(recovery.should_jump(327.7, movement, landing))
        stopped = self.participant(x=3278.713635, y=-18536.4216474, feet=495.3614187)
        self.assertFalse(recovery.should_jump(328.4, movement, stopped))
        self.assertTrue(recovery.should_jump(330.5, movement, stopped))
        proof = recovery.event['progress_evidence']
        self.assertEqual(proof['kind'], 'observed completed original point at turn')
        self.assertGreater(proof['actual_xy_progress_cm'], 25)
        self.assertLess(proof['projected_progress_cm'], 25)
        self.assertTrue(proof['completed_original_point'])
        self.assertEqual(recovery.event['original_global_index'], 263)

    def test_turn_evidence_does_not_accept_air_missing_landing_or_an_unreached_old_point(self):
        for defect in ('airborne', 'missing landing', 'landing beyond old point', 'landing wrong floor', 'fake global cursor'):
            with self.subTest(defect=defect):
                recovery, movement, landing = self.route07_turn()
                stopped = self.participant(x=3278.713635, y=-18536.4216474, feet=495.3614187)
                if defect == 'airborne':
                    stopped['falling'] = True
                elif defect == 'missing landing':
                    recovery.history[-1]['landing'] = None
                elif defect == 'landing beyond old point':
                    recovery.history[-1]['landing']['position'][0] = 3230
                elif defect == 'landing wrong floor':
                    recovery.history[-1]['landing']['feet_z'] = 400
                else:
                    recovery.history[-1]['original_global_index'] = 261
                self.assertFalse(recovery.should_jump(328.4, movement, stopped))
                if defect in ('airborne', 'missing landing'):
                    self.assertFalse(recovery.should_jump(330.5, movement, stopped))
                else:
                    with self.assertRaisesRegex(AssertionError, 'same unadvanced obstruction'):
                        recovery.should_jump(330.5, movement, stopped)
                self.assertEqual(len(recovery.events), 1)

    def test_completed_point_cannot_waive_25cm_actual_motion_or_45cm_arrival(self):
        for defect in ('under 25cm', 'outside old 45cm'):
            recovery, movement, landing = self.route07_turn()
            stopped = self.participant(x=3278.713635, y=-18536.4216474, feet=495.3614187)
            if defect == 'under 25cm':
                recovery.history[-1]['trigger']['position'] = [3270, -18530, 571]
            else:
                # On the same ground and still beyond 25 cm actual advance,
                # but 45.65 cm from the old point. Cursor alone is insufficient.
                stopped = self.participant(x=3278.713635, y=-18540.4, feet=495.3614187)
            self.assertFalse(recovery.should_jump(328.4, movement, stopped))
            with self.assertRaisesRegex(AssertionError, 'same unadvanced obstruction'):
                recovery.should_jump(330.5, movement, stopped)

    def route08_riser(self):
        recovery, movement, landing = self.route07_turn()
        recovery.should_jump(327.7, movement, landing)
        trigger = self.participant(x=3278.6518881, y=-18536.4466225, feet=495.3613996)
        self.assertFalse(recovery.should_jump(328.4, movement, trigger))
        self.assertTrue(recovery.should_jump(330.5, movement, trigger))
        recovery.observe_air(331.1, self.participant(x=3290, y=-18533, feet=550, falling=True))
        recovery.observe_air(331.9, self.participant(x=3314.5488607, y=-18525.98754, feet=512.4032212))
        return recovery, movement

    def test_route08_same_target_counts_real_forward_travel_across_edge_start(self):
        recovery, movement = self.route08_riser()
        stopped = self.participant(x=3314.6878048, y=-18526.4309059, feet=512.4032212)
        self.assertFalse(recovery.should_jump(332, movement, stopped))
        self.assertTrue(recovery.should_jump(334, movement, stopped))
        proof = recovery.event['progress_evidence']
        self.assertEqual(proof['kind'], 'signed progress on same measured edge')
        self.assertTrue(proof['same_original_target'])
        self.assertFalse(proof['completed_original_point'])
        self.assertAlmostEqual(proof['projected_progress_cm'], 36.0359167, places=4)
        self.assertAlmostEqual(proof['clamped_progress_cm'], 14.6878048, places=4)
        self.assertGreater(proof['actual_xy_progress_cm'], 37)
        self.assertEqual([event['original_global_index'] for event in recovery.events[-2:]], [263, 263])

    def test_signed_progress_does_not_approve_sideways_backward_or_under_25cm_motion(self):
        for stopped in (self.participant(x=3278.6518881, y=-18501, feet=495.3614),
                        self.participant(x=3250, y=-18536.4466, feet=495.3614),
                        self.participant(x=3298.6518881, y=-18536.4466, feet=495.3614)):
            recovery, movement = self.route08_riser()
            self.assertTrue(recovery.eligible(movement, stopped))
            self.assertFalse(recovery.should_jump(332, movement, stopped))
            with self.assertRaisesRegex(AssertionError, 'same unadvanced obstruction'):
                recovery.should_jump(334, movement, stopped)

    def test_signed_parameter_past_endpoint_never_changes_finite_distance_or_arrival(self):
        left, right = BRIDGE_POINTS[12:14]
        before, after = [3420, -18500, 543], [3455, -18500, 543]
        self.assertAlmostEqual(signed_edge_fraction(before, left, right), 1.2)
        self.assertAlmostEqual(signed_edge_fraction(after, left, right), 1.55)
        self.assertEqual(project_edge(before, left, right)[:2], (1., 20.))
        self.assertEqual(project_edge(after, left, right)[:2], (1., 55.))
        recovery = LowLedgeJump(dict(start=263, end=264, points=[right]))
        movement = dict(walk_count=1, walk_index=0)
        self.assertFalse(recovery.eligible(movement, self.participant(x=3420, y=-18500, feet=543)))
        self.assertFalse(recovery.eligible(movement, self.participant(x=3465, y=-18500, feet=543)))

    def test_original_cursor_waits_for_actual_completion_ground_and_return_endpoint(self):
        points = self.route()
        chunk = path_chunk(points, 0)
        original_observed = dict(walk_index=4, walk_count=4)
        airborne = self.participant(x=2700, feet=460, falling=True)
        overshoot = self.participant(x=3150, feet=427)
        for observation in (airborne, overshoot):
            with self.assertRaises(AssertionError):
                complete_path_chunk(chunk, 0, original_observed, observation)
        at_endpoint = self.participant(x=2700, feet=395.0148)
        with self.assertRaises(AssertionError):
            complete_path_chunk(chunk, 0, dict(walk_index=3, walk_count=4), at_endpoint)
        # A new return command's cursor cannot replace the original evidence.
        with self.assertRaises(AssertionError):
            complete_path_chunk(chunk, 0, dict(walk_index=1, walk_count=1), at_endpoint)
        self.assertEqual(complete_path_chunk(chunk, 0, original_observed, at_endpoint), 4)
        self.assertEqual(path_chunk(points, 4)['points'][0], points[4])

    def test_existing_endpoint_tolerance_is_unchanged(self):
        target = self.route()[3]
        self.assertTrue(at_ground_point(self.participant(x=2764.9, y=-18600, feet=395), target))
        self.assertFalse(at_ground_point(self.participant(x=2765, y=-18600, feet=395), target))
        self.assertFalse(at_ground_point(self.participant(x=2700, y=-18600, feet=485.1148), target))
        self.assertEqual(ledge_return_points(self.route(), path_chunk(self.route(), 0),
                                             self.participant(x=2710, feet=395)), [])


if __name__ == '__main__':
    unittest.main()
