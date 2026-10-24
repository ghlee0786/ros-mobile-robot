"""python -m unittest tooling/rpi/robot_link/test_limits.py  (ROS 없이)"""
import math
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from speed_limits import DEFAULTS, FIRMWARE, check, clamp, load, save  # noqa: E402

LIM = {'max_forward': 0.25, 'max_reverse': 0.15, 'max_angular': 0.6}


class Clamp(unittest.TestCase):
    def test_forward_cut(self):
        self.assertEqual(clamp(0.35, 0.0, LIM), (0.25, 0.0))

    def test_reverse_uses_its_own_limit(self):
        self.assertEqual(clamp(-0.35, 0.0, LIM), (-0.15, 0.0))

    def test_angular_both_ways(self):
        self.assertEqual(clamp(0.0, 0.8, LIM), (0.0, 0.6))
        self.assertEqual(clamp(0.0, -0.8, LIM), (0.0, -0.6))

    def test_inside_untouched(self):
        self.assertEqual(clamp(0.1, -0.3, LIM), (0.1, -0.3))
        self.assertEqual(clamp(-0.1, 0.3, LIM), (-0.1, 0.3))

    def test_zero_limit_stops(self):
        z = {'max_forward': 0.0, 'max_reverse': 0.0, 'max_angular': 0.0}
        self.assertEqual(clamp(0.2, 0.5, z), (0.0, 0.0))
        self.assertEqual(clamp(-0.2, -0.5, z), (0.0, 0.0))


class Check(unittest.TestCase):
    def test_range(self):
        self.assertIsNone(check('max_forward', 0.0))
        self.assertIsNone(check('max_forward', 0.35))
        self.assertIsNone(check('max_angular', 0.8))
        self.assertIsNone(check('max_reverse', 0))   # ros2 param set 은 0 을 정수로 보낸다
        self.assertIsNotNone(check('max_forward', 0.36))
        self.assertIsNotNone(check('max_angular', 0.81))
        self.assertIsNotNone(check('max_reverse', -0.1))

    def test_not_a_number(self):
        for bad in (math.nan, math.inf, 'x', True, None):
            self.assertIsNotNone(check('max_forward', bad), bad)

    def test_unknown_name(self):
        self.assertIsNotNone(check('max_speed', 0.1))

    def test_defaults_valid(self):
        for k, v in DEFAULTS.items():
            self.assertIsNone(check(k, v))
            self.assertLessEqual(v, FIRMWARE[k])


class File(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.dir.name, 'sub', 'limits.yaml')

    def tearDown(self):
        self.dir.cleanup()

    def test_missing_gives_defaults(self):
        self.assertEqual(load(self.path), DEFAULTS)

    def test_round_trip(self):
        want = {'max_forward': 0.3, 'max_reverse': 0.1, 'max_angular': 0.45}
        save(want, self.path)
        self.assertEqual(load(self.path), want)
        self.assertFalse(os.path.exists(self.path + '.tmp'))

    def test_bad_lines_fall_back(self):
        os.makedirs(os.path.dirname(self.path))
        with open(self.path, 'w', encoding='utf-8') as f:
            f.write('# 주석\nmax_forward: 0.9\nmax_reverse: abc\nmax_angular: 0.5  # 끝 주석\nother: 1\n')
        self.assertEqual(load(self.path), {'max_forward': 0.25, 'max_reverse': 0.15, 'max_angular': 0.5})


if __name__ == '__main__':
    unittest.main()
