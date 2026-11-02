"""Pin the normal square hold contract; no UE process or file fixture is used."""
import unittest

from overnight_route_scenarios import hold_deadline


class OvernightHoldContractTests(unittest.TestCase):
    def test_exact_ten_second_rule_accepts_the_full_deadline(self):
        participant = dict(extract_exit_id='OfficialExitSquare', extract_at=910)
        for seconds in (10, 10.0):
            with self.subTest(seconds=seconds):
                self.assertEqual(hold_deadline(participant, 'OfficialExitSquare', 900, seconds), 910)

    def test_shortened_rule_cannot_validate_its_own_short_deadline(self):
        for seconds in (0, 1, 9, 9.999):
            participant = dict(extract_exit_id='OfficialExitSquare', extract_at=900 + seconds)
            with self.subTest(seconds=seconds):
                with self.assertRaisesRegex(AssertionError, '10|ten'):
                    hold_deadline(participant, 'OfficialExitSquare', 900, seconds)

    def test_longer_rule_is_also_a_production_contract_change(self):
        for seconds in (10.001, 11, 30):
            participant = dict(extract_exit_id='OfficialExitSquare', extract_at=900 + seconds)
            with self.subTest(seconds=seconds):
                with self.assertRaisesRegex(AssertionError, '10|ten'):
                    hold_deadline(participant, 'OfficialExitSquare', 900, seconds)


if __name__ == '__main__':
    unittest.main()