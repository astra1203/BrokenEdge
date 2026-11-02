"""Offline checks for normal preparation of a retained, possibly empty rifle."""
import copy
import unittest

from overnight_route_scenarios import preparation_ammunition, validate_preparation_reload


class OvernightPreparationTests(unittest.TestCase):
    @staticmethod
    def player(ammo=0, reserve=60):
        return dict(weapon_item='original', ammo=ammo, carried_ammo=reserve,
                    accepted=11, combat_state=0, inventory=dict(items=[
                        dict(id='original', definition='AK', quantity=1, slot=0,
                             magazine=ammo, ammo_type='Ammo'),
                        dict(id='reserve', definition='Ammo', quantity=reserve, slot=-1),
                        dict(id='other-ammo', definition='Ammo76251', quantity=60, slot=-1)]))

    def test_empty_retained_rifle_is_valid_preparation_evidence(self):
        observed = preparation_ammunition(self.player(), 'original')
        self.assertEqual(observed['ammo'], 0)
        self.assertEqual(observed['carried_ammo'], 60)
        self.assertEqual(observed['weapon'], 'original')
        self.assertEqual(preparation_ammunition(self.player(20), 'original')['ammo'], 20)

    def test_observation_requires_real_owned_reserve_and_stored_magazine(self):
        for mutate in (lambda p: p.update(ammo=1),
                       lambda p: p.update(carried_ammo=120),
                       lambda p: p.update(combat_state=1),
                       lambda p: p['inventory']['items'][0].update(ammo_type='AmmoAP')):
            changed = self.player()
            mutate(changed)
            with self.assertRaises(AssertionError):
                preparation_ammunition(changed, 'original')

    def test_observation_rejects_substitution_duplicate_and_wrong_slot(self):
        for mutate in (lambda p: p.update(weapon_item='replacement'),
                       lambda p: p['inventory']['items'].append(copy.deepcopy(p['inventory']['items'][0])),
                       lambda p: p['inventory']['items'][0].update(slot=1),
                       lambda p: p['inventory']['items'][0].update(quantity=2)):
            changed = self.player()
            mutate(changed)
            with self.assertRaises(AssertionError):
                preparation_ammunition(changed, 'original')

    def test_normal_and_partial_reload_preserve_total_ammunition(self):
        for initial, loaded, remaining in ((60, 30, 30), (7, 7, 0)):
            before = preparation_ammunition(self.player(0, initial), 'original')
            after = preparation_ammunition(self.player(loaded, remaining), 'original')
            validate_preparation_reload(before, after)

    def test_no_reserve_or_already_loaded_cannot_claim_empty_reload(self):
        after = preparation_ammunition(self.player(30, 30), 'original')
        for ammo, reserve in ((0, 0), (5, 55)):
            before = preparation_ammunition(self.player(ammo, reserve), 'original')
            with self.assertRaises(AssertionError):
                validate_preparation_reload(before, after)

    def test_reload_rejects_created_lost_or_fired_ammunition_and_substitution(self):
        before = preparation_ammunition(self.player(), 'original')
        after = preparation_ammunition(self.player(30, 30), 'original')
        for field, value in (('ammo', 0), ('ammo', 31), ('carried_ammo', 29),
                             ('carried_ammo', -1), ('accepted', 12),
                             ('weapon', 'replacement'), ('ammo_type', 'AmmoAP')):
            changed = dict(after)
            changed[field] = value
            with self.assertRaises(AssertionError, msg=(field, value)):
                validate_preparation_reload(before, changed)


if __name__ == '__main__':
    unittest.main()
