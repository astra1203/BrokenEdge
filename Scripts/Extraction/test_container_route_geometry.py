"""Offline geometric checks; these do not simulate Unreal collision or movement."""
import copy
import math
import random
import unittest

from container_route_geometry import YawBox, capsule_edge_distance, audit_route


def world(point, yaw, offset):
    c, s = math.cos(yaw), math.sin(yaw)
    return (offset[0] + c*point[0] - s*point[1],
            offset[1] + s*point[0] + c*point[1], offset[2] + point[2])


class CapsuleEdgeTests(unittest.TestCase):
    def setUp(self):
        self.box = YawBox('box', 'group', (0, 0, 0), (1, 1, 1), 0.)

    def distance(self, start, end, radius=1, half_height=1, box=None):
        return capsule_edge_distance(start, end, box or self.box,
                                     radius=radius, half_height=half_height)

    def test_clear_endpoints_can_cross_box_in_middle(self):
        start, end = (-8, 0, 0), (8, 0, 0)
        self.assertFalse(self.distance(start, start).intersects)
        self.assertFalse(self.distance(end, end).intersects)
        result = self.distance(start, end)
        self.assertTrue(result.intersects)
        self.assertEqual(result.axis_box_distance_cm, 0)
        self.assertTrue(0 < result.parameter < 1)

    def test_corner_distance_avoids_expanded_rectangle_false_positive(self):
        result = self.distance((0, 4, 0), (4, 0, 0))
        self.assertAlmostEqual(result.parameter, .5)
        self.assertAlmostEqual(result.axis_box_distance_cm, math.sqrt(2))
        self.assertFalse(result.intersects)
        self.assertTrue(self.distance((0, 4, 0), (4, 0, 0), radius=1.5, half_height=1.5).intersects)

    def test_capsule_axis_and_different_floors_preserve_vertical_separation(self):
        self.assertTrue(self.distance((-4, 0, 5), (4, 0, 5), half_height=4).intersects)
        above = self.distance((-4, 0, 5.001), (4, 0, 5.001), half_height=4)
        self.assertFalse(above.intersects)
        self.assertAlmostEqual(above.clearance_cm, .001)
        self.assertFalse(self.distance((-4, 0, 8), (4, 0, 8), half_height=2).intersects)

    def test_rotated_translated_result_is_invariant(self):
        start, end = (-4, 2.75, -2), (3, 2.5, 4)
        reference = self.distance(start, end, radius=.75, half_height=2)
        for yaw in (.37, math.pi/2, -2.7):
            offset = (-15300, -15400, 197)
            moved = YawBox('moved', 'group', offset, self.box.half_extent, yaw)
            result = self.distance(world(start, yaw, offset), world(end, yaw, offset),
                                   radius=.75, half_height=2, box=moved)
            self.assertAlmostEqual(result.axis_box_distance_cm, reference.axis_box_distance_cm, places=9)
            self.assertAlmostEqual(result.parameter, reference.parameter, places=9)
            self.assertEqual(result.intersects, reference.intersects)

    def test_tangent_initial_overlap_and_endpoint_contact(self):
        tangent = self.distance((-4, 2, 0), (4, 2, 0))
        self.assertTrue(tangent.intersects)
        self.assertAlmostEqual(tangent.clearance_cm, 0)
        self.assertAlmostEqual(tangent.parameter, .375)
        self.assertFalse(self.distance((-4, 2.0001, 0), (4, 2.0001, 0)).intersects)
        overlap = self.distance((0, 0, 0), (5, 0, 0))
        self.assertTrue(overlap.intersects)
        self.assertEqual(overlap.parameter, 0)
        endpoint = self.distance((-4, 0, 0), (-2, 0, 0))
        self.assertTrue(endpoint.intersects)
        self.assertEqual(endpoint.parameter, 1)

    def test_zero_length_and_sloping_edges(self):
        still = self.distance((3, 0, 0), (3, 0, 0))
        self.assertEqual(still.parameter, 0)
        self.assertEqual(still.axis_box_distance_cm, 2)
        self.assertFalse(still.intersects)
        self.assertTrue(self.distance((-5, 0, 8), (5, 0, -8), half_height=3).intersects)

    def test_reversal_preserves_minimum_distance(self):
        forward = self.distance((-4, 2.4, 0), (3, 3, 2))
        backward = self.distance((3, 3, 2), (-4, 2.4, 0))
        self.assertAlmostEqual(forward.axis_box_distance_cm, backward.axis_box_distance_cm)
        self.assertAlmostEqual(forward.parameter, 1-backward.parameter)

    def test_piecewise_minimum_agrees_with_independent_dense_samples(self):
        rng = random.Random(143)
        for case in range(24):
            box = YawBox(str(case), 'g', tuple(rng.uniform(-5, 5) for _ in range(3)),
                         tuple(rng.uniform(.2, 3) for _ in range(3)), rng.uniform(-math.pi, math.pi))
            start = tuple(rng.uniform(-9, 9) for _ in range(3))
            end = tuple(rng.uniform(-9, 9) for _ in range(3))
            radius, half = rng.uniform(.2, 1), rng.uniform(1, 4)
            exact = self.distance(start, end, radius, half, box)
            sampled = math.inf
            steps = 1000
            for index in range(steps+1):
                t = index / steps
                p = box.local(tuple(a+(b-a)*t for a, b in zip(start, end)))
                dx = max(-box.half_extent[0]-p[0], 0, p[0]-box.half_extent[0])
                dy = max(-box.half_extent[1]-p[1], 0, p[1]-box.half_extent[1])
                axis_bottom, axis_top = p[2]-(half-radius), p[2]+(half-radius)
                dz = max(-box.half_extent[2]-axis_top, 0, axis_bottom-box.half_extent[2])
                sampled = min(sampled, math.sqrt(dx*dx+dy*dy+dz*dz))
            self.assertLessEqual(exact.axis_box_distance_cm, sampled+1e-10)
            self.assertLessEqual(sampled-exact.axis_box_distance_cm, math.dist(start, end)/steps+1e-10)

    def test_invalid_shapes_and_tilted_box_are_rejected(self):
        for radius, half in ((0, 1), (2, 1), (math.nan, 1), (1, math.inf)):
            with self.assertRaises(ValueError):
                self.distance((0, 0, 0), (1, 0, 0), radius, half)
        with self.assertRaises(ValueError):
            self.distance((math.nan, 0, 0), (1, 0, 0))
        point = dict(id='box', group='g', derived_native=dict(
            world_box_center_cm=[0, 0, 0], world_half_extent_cm=[1, 2, 3],
            world_rotation_xyzw=[math.sin(.1), 0, 0, math.cos(.1)]))
        with self.assertRaisesRegex(ValueError, 'yaw-only'):
            YawBox.from_asset_point(point)
        point['derived_native']['world_rotation_xyzw'] = [0, 0, 0, 2]
        with self.assertRaisesRegex(ValueError, 'unit quaternion'):
            YawBox.from_asset_point(point)
        point['derived_native']['world_rotation_xyzw'] = [0, 0, math.sin(.4), math.cos(.4)]
        self.assertAlmostEqual(YawBox.from_asset_point(point).yaw_radians, .8)


class SelectedRouteAuditTests(unittest.TestCase):
    def setUp(self):
        points = []
        for identifier, x in [('7F_box1', 0), ('7F_box2', 100)]:
            points.append(dict(id=identifier, group='choice', enabled=True, weight=1, kind='Supply',
                               transform=dict(translation_cm=[x, 0, 2]), derived_native=dict(
                                   world_box_center_cm=[x, 0, 2], world_half_extent_cm=[1, 1, 1],
                                   world_rotation_xyzw=[0, 0, 0, 1])))
        self.audit = dict(export_complete=True, asset_files_unchanged=True, result='passed',
                          candidate_count=2, points=points, cdo=dict(
                              container_relative_rotation_pitch_yaw_roll=[0, 0, 0],
                              container_relative_scale3d=[1, 1, 1]))
        self.snapshot = dict(loot=[dict(source_point='7F_box1', loose=False, position=[0, 0, 2])],
                             players=[dict(name='EX_'+r, capsule_radius=1, capsule_half=2) for r in ('A', 'B')])
        self.route = dict(nodes={'startA': dict(kind='spawn'), 'startB': dict(kind='spawn'),
                                 '7F_box1': dict(kind='loot'), '7F_box2': dict(kind='loot'),
                                 'peer': dict(kind='peer_parking', parent='7F_box1')}, pair_legs=[dict(
                                     a_start='startA', b_start='startB', destination='7F_box1', b_end='peer',
                                     A=dict(points=[[-5, 0, 0], [5, 0, 0]]),
                                     B=dict(points=[[-5, 0, 10], [5, 0, 10]]))])
        excluded = copy.deepcopy(self.route['pair_legs'][0])
        excluded['destination'] = '7F_box2'
        self.route['pair_legs'].append(excluded)

    def test_actual_selection_filters_variants_and_feet_are_converted_to_centres(self):
        result = audit_route(self.audit, self.route, self.snapshot)
        self.assertFalse(result['runtime_proof'])
        self.assertEqual(result['summary']['compatible_pairs'], 1)
        self.assertEqual(result['summary']['skipped_pairs'], 1)
        self.assertEqual(result['summary']['intersecting_box_edges'], 1)
        self.assertEqual(result['conflicts'][0]['role'], 'A')
        self.assertEqual(result['conflicts'][0]['closest']['center_cm'][2], 2)
        self.assertEqual(result['selected_containers'], ['7F_box1'])

    def test_missing_duplicate_unknown_or_moved_snapshot_boxes_fail_closed(self):
        for mutate in (lambda s: s.update(loot=[]),
                       lambda s: s['loot'].append(copy.deepcopy(s['loot'][0])),
                       lambda s: s['loot'][0].update(source_point='7F_unknown'),
                       lambda s: s['loot'][0].update(position=[2, 0, 2])):
            changed = copy.deepcopy(self.snapshot)
            mutate(changed)
            with self.assertRaises(ValueError):
                audit_route(self.audit, self.route, changed)


if __name__ == '__main__':
    unittest.main()
