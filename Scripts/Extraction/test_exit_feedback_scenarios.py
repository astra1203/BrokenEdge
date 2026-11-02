"""Regression for the owner-only exit-feedback acceptance boundary."""
import unittest
from exit_feedback_scenarios import default_feedback, feedback_matches


class ExitFeedbackReplicationTests(unittest.TestCase):
    def fixture(self, role='A', feedback=None):
        defaults = dict(extract_exit_id='None', extract_blocked_reason='', extract_at=0, life=0)
        private = dict(extract_exit_id='OfficialExitSquare', extract_blocked_reason='not open', extract_at=0)
        if feedback is not None:
            private = feedback
        states = {peer: dict(defaults) for peer in ('Server', 'A', 'B')}
        for peer in ('Server', role):
            states[peer].update(private)
        def read_player(peer, requested_role):
            self.assertEqual(requested_role, role)
            return states[peer]
        def expected(value):
            return all(value.get(key) == expected_value for key, expected_value in private.items())
        return states, read_player, expected

    def test_authority_and_owner_receive_feedback_while_observer_keeps_defaults(self):
        for role in ('A', 'B'):
            for feedback in (
                dict(extract_exit_id='OfficialExitSquare', extract_blocked_reason='not open', extract_at=0),
                dict(extract_exit_id='OfficialExitSquare', extract_blocked_reason='', extract_at=350.25),
                dict(extract_exit_id='OfficialExitSquare', extract_blocked_reason='capacity exhausted', extract_at=0),
                dict(extract_exit_id='None', extract_blocked_reason='', extract_at=0),
            ):
                with self.subTest(role=role, feedback=feedback):
                    _, read, expected = self.fixture(role, feedback)
                    self.assertTrue(feedback_matches(read, role, expected))

    def test_each_private_field_leaking_to_observer_fails(self):
        for role in ('A', 'B'):
            observer = 'B' if role == 'A' else 'A'
            for field, value in (('extract_exit_id', 'OfficialExitSquare'),
                                 ('extract_blocked_reason', 'not open'), ('extract_at', 350.25)):
                with self.subTest(role=role, field=field):
                    states, read, expected = self.fixture(role)
                    states[observer][field] = value
                    self.assertFalse(feedback_matches(read, role, expected))

    def test_missing_authority_or_owner_feedback_fails(self):
        for peer in ('Server', 'A'):
            with self.subTest(peer=peer):
                states, read, expected = self.fixture()
                states[peer]['extract_exit_id'] = 'None'
                self.assertFalse(feedback_matches(read, 'A', expected))

    def test_public_life_must_remain_alive_on_all_peers(self):
        for peer in ('Server', 'A', 'B'):
            with self.subTest(peer=peer):
                states, read, expected = self.fixture()
                states[peer]['life'] = 3
                self.assertFalse(feedback_matches(read, 'A', expected))

    def test_missing_observer_player_does_not_pass_as_default(self):
        states, read, expected = self.fixture()
        states['B'] = {}
        self.assertFalse(feedback_matches(read, 'A', expected))
        self.assertFalse(default_feedback({}))


if __name__ == '__main__':
    unittest.main()
