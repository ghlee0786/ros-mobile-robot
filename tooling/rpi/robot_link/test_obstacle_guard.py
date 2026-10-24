"""python -m unittest tooling/rpi/robot_link/test_obstacle_guard.py  (no ROS needed)"""
import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from obstacle_guard import allowed_speed, guard, nearest_in_sector  # noqa: E402

N = 360
INC = 2 * math.pi / N
AMIN = -math.pi


def scan_with(hits):
    """hits: {bearing_deg: range}; everything else empty."""
    r = [math.inf] * N
    for deg, dist in hits.items():
        r[int(round((math.radians(deg) - AMIN) / INC)) % N] = dist
    return r, AMIN, INC


class Guard(unittest.TestCase):
    def test_wall_ahead_stops_forward(self):
        v, near = guard(0.35, scan_with({0: 0.31}), stop_dist=0.35, dec=0.6, latency=0.2, half_width=math.radians(35))
        self.assertEqual(v, 0.0)
        self.assertAlmostEqual(near, 0.31)

    def test_far_wall_allows_full_speed(self):
        v, _ = guard(0.35, scan_with({0: 3.0}), 0.35, 0.6, 0.2, math.radians(35))
        self.assertEqual(v, 0.35)

    def test_speed_shrinks_as_wall_nears(self):
        speeds = [guard(0.35, scan_with({0: d}), 0.35, 0.6, 0.2, math.radians(35))[0] for d in (1.0, 0.7, 0.5, 0.4)]
        self.assertEqual(speeds, sorted(speeds, reverse=True))
        self.assertLess(speeds[-1], 0.2)

    def test_stops_in_time(self):
        # at the allowed speed, reaction + braking distance fits in the room left
        for d in (0.5, 0.8, 1.2):
            v = allowed_speed(d, 0.35, 0.6, 0.2)
            self.assertLessEqual(v * 0.2 + v * v / (2 * 0.6), d - 0.35 + 1e-9)

    def test_wall_behind_does_not_block_forward(self):
        v, _ = guard(0.3, scan_with({180: 0.25}), 0.35, 0.6, 0.2, math.radians(35))
        self.assertEqual(v, 0.3)

    def test_wall_behind_blocks_reverse(self):
        v, _ = guard(-0.2, scan_with({180: 0.3}), 0.35, 0.6, 0.2, math.radians(35))
        self.assertEqual(v, 0.0)

    def test_side_obstacle_ignored(self):
        v, _ = guard(0.3, scan_with({90: 0.3, -90: 0.3}), 0.35, 0.6, 0.2, math.radians(35))
        self.assertEqual(v, 0.3)

    def test_self_hits_ignored(self):
        self.assertEqual(nearest_in_sector(*scan_with({0: 0.16}), 0.0, math.radians(35)), math.inf)

    def test_turn_in_place_untouched(self):
        self.assertEqual(guard(0.0, scan_with({0: 0.2}), 0.35, 0.6, 0.2, math.radians(35))[0], 0.0)


if __name__ == '__main__':
    unittest.main()
