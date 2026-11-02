"""Synthetic regression for guard evidence validation; not a live game acceptance."""
import unittest
from copy import deepcopy
from unittest.mock import patch

from guard_tuning_scenarios import (collect_shots, guard_interval, prepare_reload_target, reload_fixture_positions,
                                    guard_loot_ready, raid_item_owners, request_item_with_receipt,
                                    reload_target_ready, reserve_ammo, select_guard,
                                    validate_damage, validate_reload, validate_timing)


class GuardTuningTests(unittest.TestCase):
    def setUp(self):
        self.rules = dict(reaction_seconds=2, shot_interval=.15, damage_scale=.15)
        self.weapon = dict(fire_delay=.1, damage=20, head_damage=40)

    @staticmethod
    def events(start=1, count=5, first=12.01, interval=1 / 6):
        return {start + i: first + interval * i for i in range(count)}

    @staticmethod
    def snapshot(events):
        return dict(shots=max(events), shot_times=[dict(sequence=key, time=value)
                                                  for key, value in events.items()])

    def test_current_defaults_use_authoritative_weapon_limit(self):
        result = validate_timing(self.events(), 0, self.rules, self.weapon, 12)
        self.assertAlmostEqual(result['reaction_seconds'], 2.01)
        self.assertEqual(result['expected_interval_seconds'], .15)
        self.assertEqual(len(result['shot_intervals']), 4)

    def test_slower_weapon_takes_precedence(self):
        self.weapon['fire_delay'] = .3
        self.assertEqual(guard_interval(self.rules, self.weapon), .3)
        validate_timing(self.events(interval=.32), 0, self.rules, self.weapon, 12)

    def test_zero_override_uses_weapon_delay(self):
        self.rules['shot_interval'] = 0
        self.weapon['fire_delay'] = .06
        self.assertEqual(guard_interval(self.rules, self.weapon), .06)
        validate_timing(self.events(interval=1 / 15), 0, self.rules, self.weapon, 12)

    def test_snapshots_can_skip_multiple_shots_and_repeat(self):
        captured = {}
        first = self.snapshot(self.events(count=2))
        collect_shots(captured, first, 0)
        collect_shots(captured, first, 0)
        collect_shots(captured, self.snapshot(self.events(count=6)), 0)
        self.assertEqual(len(captured), 6)
        validate_timing(captured, 0, self.rules, self.weapon, 12)

    def test_ring_rollover_retains_events_already_collected(self):
        captured = {}
        collect_shots(captured, self.snapshot(self.events(start=101, count=64)), 100)
        all_events = self.events(start=101, count=70)
        recent = {key: value for key, value in all_events.items() if key >= 107}
        collect_shots(captured, self.snapshot(recent), 100)
        self.assertEqual(len(captured), 70)
        validate_timing(captured, 100, self.rules, self.weapon, 12)

    def test_missing_authority_event_is_not_excused_as_snapshot_loss(self):
        missing = self.events()
        del missing[3]
        with self.assertRaisesRegex(AssertionError, 'history is incomplete'):
            collect_shots({}, self.snapshot(missing), 0)

    def test_timestamp_changes_and_counter_resets_fail(self):
        captured = self.events()
        changed = captured.copy()
        changed[3] += .01
        with self.assertRaisesRegex(AssertionError, 'timestamp changed'):
            collect_shots(captured, self.snapshot(changed), 0)
        with self.assertRaisesRegex(AssertionError, 'counter reset'):
            collect_shots({}, dict(shots=1, shot_times=[]), 3)

    def test_old_slow_cadence_and_burst_with_correct_average_both_fail(self):
        with self.assertRaisesRegex(AssertionError, 'cadence'):
            validate_timing(self.events(interval=.8), 0, self.rules, self.weapon, 12)
        burst = self.events()
        burst[2] = burst[1] + .01
        with self.assertRaisesRegex(AssertionError, 'cadence'):
            validate_timing(burst, 0, self.rules, self.weapon, 12)

    def test_early_and_late_reactions_fail(self):
        for first in (11.9, 12.5):
            with self.subTest(first=first), self.assertRaisesRegex(AssertionError, 'reaction deadline'):
                validate_timing(self.events(first=first), 0, self.rules, self.weapon, 12)

    def test_current_damage_is_three_body_six_head(self):
        self.assertTrue(validate_damage([dict(loss=3, bone='spine_03'), dict(loss=6, bone='head')],
                                        self.rules, self.weapon))

    def test_stale_damage_scale_is_detected(self):
        with self.assertRaisesRegex(AssertionError, 'damage'):
            validate_damage([dict(loss=6, bone='spine_03'), dict(loss=12, bone='head')],
                            self.rules, self.weapon)

    def test_missing_hits_and_head_only_samples_do_not_pass(self):
        for hits in ([], [dict(loss=3, bone='spine_03')], [dict(loss=6, bone='head')] * 2):
            with self.subTest(hits=hits), self.assertRaisesRegex(AssertionError, 'at least two'):
                validate_damage(hits, self.rules, self.weapon)

    def test_invalid_runtime_data_fails_instead_of_falling_back(self):
        for delay in (0, float('nan'), float('inf')):
            with self.subTest(delay=delay), self.assertRaises(AssertionError):
                guard_interval(self.rules, dict(self.weapon, fire_delay=delay))
        with self.assertRaises(KeyError):
            guard_interval(self.rules, {})
        with self.assertRaises(AssertionError):
            collect_shots({}, dict(shots=1, shot_times=[dict(sequence=1, time=float('nan'))]), 0)

    def test_guard_selection_uses_authored_route_instead_of_spawn_index(self):
        guards = [dict(name='GUARD_0', route='ExtractionPatrol_7F_EastIndustry'),
                  dict(name='GUARD_3', route='ExtractionPatrol_1'),
                  dict(name='GUARD_2', route='ExtractionPatrol_0')]
        self.assertEqual(select_guard(guards)['name'], 'GUARD_2')
        self.assertEqual(select_guard(list(reversed(guards)))['name'], 'GUARD_2')
        self.assertEqual(select_guard(guards[:2])['name'], 'GUARD_3')
        with self.assertRaisesRegex(AssertionError, 'central patrol'):
            select_guard(guards[:1])

    def test_reload_evidence_accounts_for_shots_after_completion(self):
        before = dict(ammo=0, carried_ammo=60, shots=7)
        after = dict(ammo=27, carried_ammo=30, shots=10, combat_state=0)
        self.assertTrue(validate_reload(before, after, 'shots'))
        before = dict(ammo=0, carried_ammo=120, accepted=4)
        after = dict(ammo=30, carried_ammo=90, accepted=4, combat_state=0)
        self.assertTrue(validate_reload(before, after, 'accepted'))

    def test_previously_consumed_reserve_cannot_fake_a_new_reload(self):
        before = dict(ammo=0, carried_ammo=30, shots=7)
        after = dict(ammo=30, carried_ammo=30, shots=7, combat_state=0)
        with self.assertRaisesRegex(AssertionError, 'not conserved'):
            validate_reload(before, after, 'shots')
        after['carried_ammo'] = 1
        with self.assertRaisesRegex(AssertionError, 'not conserved'):
            validate_reload(before, after, 'shots')

    def test_loaded_or_unfinished_weapon_cannot_fake_reload_success(self):
        before = dict(ammo=5, carried_ammo=60, shots=7)
        after = dict(ammo=30, carried_ammo=35, shots=7, combat_state=0)
        with self.assertRaisesRegex(AssertionError, 'empty magazine'):
            validate_reload(before, after, 'shots')
        before['ammo'] = 0
        after.update(carried_ammo=30, combat_state=1)
        with self.assertRaisesRegex(AssertionError, 'must finish'):
            validate_reload(before, after, 'shots')

    def test_reserve_excludes_loaded_rounds_and_unrelated_items(self):
        inventory = dict(items=[dict(definition='AK', quantity=1, magazine=30),
                                dict(definition='Ammo', quantity=18),
                                dict(definition='Ammo', quantity=12),
                                dict(definition='Medkit', quantity=1)])
        self.assertEqual(reserve_ammo(inventory), 30)


class GuardReloadPreparationTests(unittest.TestCase):
    @staticmethod
    def snapshot(*, acquired=False, world_time=68):
        return dict(world_time=world_time, command=46, smokes=[], guards=[dict(
            name='GUARD_2', position=[1400, 4500, 445], health=100, dead=False,
            ammo=19, carried_ammo=60, combat_state=0, shots=11,
            state=1 if acquired else 0, target='Pawn_A' if acquired else 'None',
            line_of_sight_to_A=True, smoke_blocked_to_A=False,
            reaction_ready_time=world_time + 2 if acquired else 52.0683)], players=[dict(
                name='EX_A', pawn_name='Pawn_A', position=[1400, 3900, 445], life=0, health=100)])

    def test_positions_put_player_toward_observed_patrol_side(self):
        player, guard, yaw = reload_fixture_positions([438, -568, 458])
        self.assertEqual((player, guard, yaw), ([1400, 3900, 500], [1400, 4500, 500], -90))
        player, guard, yaw = reload_fixture_positions([1400, 9000, 458])
        self.assertEqual((player, guard, yaw), ([1400, 4500, 500], [1400, 3900, 500], 90))
        with self.assertRaisesRegex(AssertionError, 'Invalid patrol'):
            reload_fixture_positions([0, float('nan'), 0])

    def test_acquisition_requires_real_target_geometry_loaded_weapon_and_freshness(self):
        ready = self.snapshot(acquired=True, world_time=69)
        self.assertTrue(reload_target_ready(ready, 'GUARD_2', 68, 52.0683))
        bad_guard_fields = dict(target='Pawn_B', state=0, line_of_sight_to_A=False,
                                smoke_blocked_to_A=True, dead=True, health=0, ammo=0,
                                carried_ammo=0, combat_state=1, reaction_ready_time=52.0683)
        for key, value in bad_guard_fields.items():
            with self.subTest(field=key):
                snapshot = deepcopy(ready)
                snapshot['guards'][0][key] = value
                self.assertFalse(reload_target_ready(snapshot, 'GUARD_2', 68, 52.0683))
        for key, value in [('world_time', 68), ('smokes', [dict(radius=550)])]:
            with self.subTest(field=key):
                snapshot = deepcopy(ready)
                snapshot[key] = value
                self.assertFalse(reload_target_ready(snapshot, 'GUARD_2', 68, 52.0683))
        ready['players'][0]['life'] = 1
        self.assertFalse(reload_target_ready(ready, 'GUARD_2', 68, 52.0683))

    def test_missing_driver_contract_is_not_treated_as_acquisition(self):
        snapshot = self.snapshot(acquired=True, world_time=69)
        del snapshot['guards'][0]['line_of_sight_to_A']
        with self.assertRaises(KeyError):
            reload_target_ready(snapshot, 'GUARD_2', 68, 52.0683)

    def run_preparation(self, succeeds_on_attempt=None, becomes_ready_after=None):
        clock, placements, evidence = [0.0], [], {}

        def sleep(seconds):
            clock[0] += seconds

        def snapshot():
            acquired = ((succeeds_on_attempt is not None and len(placements) >= succeeds_on_attempt)
                        or (becomes_ready_after is not None and clock[0] >= becomes_ready_after))
            return self.snapshot(acquired=acquired, world_time=68 + clock[0])

        with patch('guard_tuning_scenarios.time.monotonic', side_effect=lambda: clock[0]), \
                patch('guard_tuning_scenarios.time.sleep', side_effect=sleep):
            try:
                result = prepare_reload_target(placements.append, snapshot, 'GUARD_2', [438, -568, 458],
                                               evidence, max_attempts=3, attempt_seconds=.1)
            except AssertionError as error:
                return placements, evidence, error
        return placements, evidence, result

    def test_retry_records_real_acquisition_and_stops_placing_after_success(self):
        placements, evidence, acquired = self.run_preparation(succeeds_on_attempt=2)
        self.assertEqual(len(placements), 2)
        self.assertEqual(len(evidence['attempts']), 2)
        self.assertEqual([actor['name'] for actor in placements[0]], ['EX_A', 'GUARD_2'])
        self.assertEqual(evidence['acquired'], acquired)
        self.assertTrue(reload_target_ready(acquired, 'GUARD_2', 68, 52.0683))
        self.assertEqual(acquired['guards'][0]['ammo'], 19)  # Preparation never empties/reloads the gun.
        placements[1][0]['position'][0] = -999
        self.assertEqual(evidence['attempts'][1]['requested_players'][0]['position'][0], 1400)

    def test_late_acquisition_is_not_teleported_away_by_next_retry(self):
        placements, evidence, acquired = self.run_preparation(becomes_ready_after=.1)
        self.assertEqual(len(placements), 1)
        self.assertEqual(evidence['acquired'], acquired)
        self.assertTrue(reload_target_ready(acquired, 'GUARD_2', 68, 52.0683))

    def test_failed_acquisition_is_bounded_and_has_distinct_failure_evidence(self):
        placements, evidence, error = self.run_preparation()
        self.assertIsInstance(error, AssertionError)
        self.assertIn('acquisition prerequisite failed after 3 fixture attempts', str(error))
        self.assertEqual(len(placements), 3)
        self.assertNotIn('acquired', evidence)
        self.assertEqual(evidence['attempts'][-1]['last']['guards'][0]['target'], 'None')

    def test_preparation_never_revives_a_downed_player(self):
        snapshot = self.snapshot()
        snapshot['players'][0]['life'] = 1
        placements, evidence = [], {}
        with self.assertRaisesRegex(AssertionError, 'cannot revive'):
            prepare_reload_target(placements.append, lambda: snapshot, 'GUARD_2', [438, -568, 458], evidence)
        self.assertFalse(placements)


class GuardLootPreparationTests(unittest.TestCase):
    def setUp(self):
        self.rifle = dict(id='guard-rifle', definition='AK', quantity=1, slot=0, magazine=0)
        self.scar = dict(id='original-scar', definition='SCARH', quantity=1, slot=1, magazine=20)
        self.box = dict(id='corpse', contents=dict(id='corpse', revision=21, items=[self.rifle]))
        self.server = dict(inventory=dict(id='player-inventory', revision=10, items=[self.scar]),
                           inventory_open=True, loot_id='corpse', loot_view=deepcopy(self.box['contents']),
                           session=1, search_end=0)
        self.owner = deepcopy(self.server)

    def clear_slot(self):
        self.server['inventory']['items'][0]['slot'] = -1
        self.owner = deepcopy(self.server)

    def test_revealed_rifle_does_not_excuse_an_occupied_secondary_slot(self):
        self.assertFalse(guard_loot_ready(self.server, self.owner, self.box, 'guard-rifle'))
        self.clear_slot()
        self.assertTrue(guard_loot_ready(self.server, self.owner, self.box, 'guard-rifle'))

    def test_search_and_revision_prerequisites_are_required(self):
        self.clear_slot()
        for field, value in [('search_end', 1), ('session', 2), ('inventory_open', False), ('loot_id', 'other')]:
            with self.subTest(field=field):
                owner = deepcopy(self.owner)
                owner[field] = value
                self.assertFalse(guard_loot_ready(self.server, owner, self.box, 'guard-rifle'))
        for field in ('loot_view', 'inventory'):
            with self.subTest(field=field):
                owner = deepcopy(self.owner)
                owner[field]['revision'] -= 1
                self.assertFalse(guard_loot_ready(self.server, owner, self.box, 'guard-rifle'))
        self.owner['loot_view']['items'][0]['definition'] = 'None'
        self.assertFalse(guard_loot_ready(self.server, self.owner, self.box, 'guard-rifle'))

    def test_original_weapon_has_one_owner_before_and_after_storage(self):
        snapshot = dict(players=[dict(name='EX_A', inventory=self.server['inventory'])], guards=[], loot=[])
        self.assertEqual(raid_item_owners(snapshot, 'original-scar'), ['player:EX_A'])
        self.clear_slot()
        self.assertEqual(raid_item_owners(snapshot, 'original-scar'), ['player:EX_A'])
        snapshot['loot'].append(dict(id='duplicate-drop', contents=dict(items=[deepcopy(self.scar)])))
        self.assertEqual(raid_item_owners(snapshot, 'original-scar'), ['player:EX_A', 'loot:duplicate-drop'])

    @staticmethod
    def wait_for_reply(predicate, _message, _timeout):
        for _ in range(3):
            if predicate():
                return
        raise AssertionError('Test reply did not arrive')

    def test_item_request_waits_for_a_new_receipt_and_preserves_it(self):
        replies = iter([dict(item_sequence=4),
                        dict(world_time=68, item_sequence=4, item_message='old result'),
                        dict(world_time=69, item_sequence=5, item_message='')])
        sent, evidence = [], []
        request = dict(item='guard-rifle', from_loot=True, quantity=1, slot=1)
        result = request_item_with_receipt(lambda **fields: sent.append(fields), lambda: next(replies),
                                           self.wait_for_reply, request, evidence)
        self.assertEqual(sent, [request])
        self.assertEqual(evidence, [result])
        self.assertEqual(result['reply'], dict(world_time=69, item_sequence=5, item_message=''))

    def test_rejected_item_request_fails_immediately_and_keeps_the_real_error(self):
        error = '空间不足、位置占用或无法堆叠'
        replies = iter([dict(item_sequence=0), dict(world_time=69, item_sequence=1, item_message=error)])
        evidence = []
        with self.assertRaisesRegex(AssertionError, error):
            request_item_with_receipt(lambda **_fields: None, lambda: next(replies), self.wait_for_reply,
                                      dict(item='guard-rifle', from_loot=True, slot=1), evidence)
        self.assertEqual(evidence[0]['reply']['item_message'], error)

    def test_unrelated_later_receipt_cannot_prove_this_operation(self):
        replies = iter([dict(item_sequence=0), dict(world_time=69, item_sequence=2, item_message='')])
        with self.assertRaisesRegex(AssertionError, 'concurrent'):
            request_item_with_receipt(lambda **_fields: None, lambda: next(replies), self.wait_for_reply,
                                      dict(item='guard-rifle', from_loot=True, slot=1), [])

if __name__ == '__main__':
    unittest.main()
