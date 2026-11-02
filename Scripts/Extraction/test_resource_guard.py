"""Pure threshold tests: no process launch, termination, UE or machine sampling."""
import unittest
from resource_guard import DEFAULT_LIMITS, preflight_reason, runtime_reason, validate_limits


class ResourceGuardThresholdTests(unittest.TestCase):
    def limits(self, **overrides):
        value = dict(DEFAULT_LIMITS)
        value.update(overrides)
        validate_limits(value)
        return value

    def sample(self, c=100, d=150, ram=16):
        return dict(c_free_gib=c, d_free_gib=d, ram_free_gib=ram)

    def test_original_defaults_and_new_critical_limit(self):
        self.assertEqual(DEFAULT_LIMITS, dict(start_c_gib=40, start_d_gib=100, start_ram_gib=8,
            stop_c_gib=25, stop_d_gib=60, stop_ram_gib=3, critical_ram_gib=1, low_ram_seconds=15))
        validate_limits(DEFAULT_LIMITS)

    def test_all_limits_reject_negative_nan_and_infinity(self):
        for name in DEFAULT_LIMITS:
            for invalid in (-1, float('nan'), float('inf'), -float('inf')):
                with self.subTest(name=name, invalid=invalid):
                    with self.assertRaises(ValueError):
                        self.limits(**{name: invalid})

    def test_start_stop_and_critical_ordering(self):
        for overrides in (dict(start_c_gib=24), dict(start_d_gib=59),
                          dict(start_ram_gib=2), dict(critical_ram_gib=4)):
            with self.subTest(overrides=overrides):
                with self.assertRaises(ValueError):
                    self.limits(**overrides)
        validate_limits({name: 0 for name in DEFAULT_LIMITS})

    def test_preflight_checks_each_resource_and_allows_exact_threshold(self):
        limits = self.limits()
        self.assertIsNone(preflight_reason(self.sample(c=40, d=100, ram=8), limits))
        for resource, observed in (('c', 39.9), ('d', 99.9), ('ram', 7.9)):
            with self.subTest(resource=resource):
                reason = preflight_reason(self.sample(**{resource: observed}), limits)
                self.assertEqual(reason['code'], 'start_' + resource + '_gib')
                self.assertEqual(reason['observed_gib'], observed)
                self.assertEqual(reason['comparison'], '<')

    def test_requested_freshcook_limits_replace_old_d_disk_gate(self):
        limits = self.limits(start_d_gib=40, stop_d_gib=25, start_ram_gib=8,
                             stop_ram_gib=2, critical_ram_gib=1)
        self.assertIsNone(preflight_reason(self.sample(d=40, ram=8), limits))
        self.assertEqual(preflight_reason(self.sample(d=40, ram=8), DEFAULT_LIMITS)['code'], 'start_d_gib')
        _, reason = runtime_reason(self.sample(d=24.9), limits, 100, None)
        self.assertEqual(reason['code'], 'stop_d_gib')
        self.assertEqual(reason['limit_gib'], 25)
        self.assertIsNone(runtime_reason(self.sample(c=25, d=25, ram=2), limits, 100, None)[1])

    def test_critical_ram_fails_on_first_sample_without_grace(self):
        limits = self.limits()
        low_since, reason = runtime_reason(self.sample(ram=.9), limits, 0, None)
        self.assertEqual(low_since, 0)
        self.assertEqual(reason['code'], 'critical_ram_gib')
        self.assertEqual(reason['limit_gib'], 1)
        self.assertNotIn('grace_seconds', reason)
        self.assertIsNone(runtime_reason(self.sample(ram=1), limits, 0, None)[1])

    def test_sustained_low_ram_includes_zero_start_and_exact_grace_boundary(self):
        limits = self.limits()
        start, reason = runtime_reason(self.sample(ram=2), limits, 0, None)
        self.assertEqual(start, 0)
        self.assertIsNone(reason)
        start, reason = runtime_reason(self.sample(ram=2), limits, 14.9, start)
        self.assertEqual(start, 0)
        self.assertIsNone(reason)
        start, reason = runtime_reason(self.sample(ram=2), limits, 15, start)
        self.assertEqual(reason['code'], 'stop_ram_gib')
        self.assertEqual(reason['low_ram_seconds'], 15)
        self.assertEqual(reason['grace_seconds'], 15)

    def test_recovery_resets_low_ram_interval(self):
        limits = self.limits()
        start, _ = runtime_reason(self.sample(ram=2), limits, 0, None)
        start, reason = runtime_reason(self.sample(ram=3), limits, 14, start)
        self.assertIsNone(start)
        self.assertIsNone(reason)
        start, reason = runtime_reason(self.sample(ram=2), limits, 20, start)
        self.assertEqual(start, 20)
        self.assertIsNone(reason)
        self.assertIsNone(runtime_reason(self.sample(ram=2), limits, 34.9, start)[1])
        self.assertEqual(runtime_reason(self.sample(ram=2), limits, 35, start)[1]['code'], 'stop_ram_gib')

    def test_zero_grace_stops_on_first_low_ram_sample(self):
        limits = self.limits(low_ram_seconds=0)
        self.assertEqual(runtime_reason(self.sample(ram=2), limits, 0, None)[1]['code'], 'stop_ram_gib')

    def test_disk_stops_immediately_and_critical_ram_takes_priority(self):
        for resource, observed in (('c', 24.9), ('d', 59.9)):
            with self.subTest(resource=resource):
                reason = runtime_reason(self.sample(**{resource: observed}), DEFAULT_LIMITS, 0, None)[1]
                self.assertEqual(reason['code'], 'stop_' + resource + '_gib')
        reason = runtime_reason(self.sample(c=1, d=1, ram=.5), DEFAULT_LIMITS, 0, None)[1]
        self.assertEqual(reason['code'], 'critical_ram_gib')


if __name__ == '__main__':
    unittest.main()
