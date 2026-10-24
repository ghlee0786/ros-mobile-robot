"""nav_goal_bridge 의 순수 함수 시험(ROS 없이): python -m unittest tooling/nav/test_nav_goal_bridge.py"""
import json
import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nav_goal_bridge as nb  # noqa: E402


def grid(w=10, h=10, res=0.1, origin=(0.0, 0.0, 0.0), fill=0):
    return {'resolution': res, 'width': w, 'height': h, 'origin': origin, 'data': [fill] * (w * h)}


class CellCost(unittest.TestCase):
    def test_index_row_major(self):
        g = grid()
        g['data'][3 * 10 + 2] = 100
        self.assertEqual(nb.cell_cost(g, 0.25, 0.35), 100)
        self.assertEqual(nb.cell_cost(g, 0.35, 0.25), 0)

    def test_outside(self):
        g = grid()
        for x, y in ((-0.01, 0.5), (0.5, -0.01), (1.0, 0.5), (0.5, 1.0)):
            self.assertEqual(nb.cell_cost(g, x, y), nb.OUTSIDE)

    def test_origin_offset_and_yaw(self):
        g = grid(origin=(-1.0, -1.0, 0.0))
        g['data'][0] = 99
        self.assertEqual(nb.cell_cost(g, -0.95, -0.95), 99)
        # 90° 돌린 지도: 지도 +x 축이 세계 +y
        g = grid(origin=(0.0, 0.0, math.pi / 2))
        g['data'][0 * 10 + 4] = 100
        self.assertEqual(nb.cell_cost(g, -0.05, 0.45), 100)


class FailReason(unittest.TestCase):
    def test_start_blocked_by_costmap_even_with_generic_code(self):
        for code in (0, 200, 208, 9000):
            self.assertEqual(nb.fail_reason(code, '', 99, 0), nb.START_BLOCKED)
        self.assertIn('지도 초기화', nb.START_BLOCKED)
        self.assertIn('0.3 m', nb.START_BLOCKED)

    def test_start_blocked_by_code(self):
        self.assertEqual(nb.fail_reason(205, '', None, None), nb.START_BLOCKED)

    def test_free_start_is_not_blocked(self):
        self.assertNotEqual(nb.fail_reason(208, '', 98, 0), nb.START_BLOCKED)
        self.assertEqual(nb.fail_reason(208, '', 98, 0), nb.NO_PATH)

    def test_goal(self):
        self.assertEqual(nb.fail_reason(208, '', 0, 100), nb.GOAL_BLOCKED)
        self.assertEqual(nb.fail_reason(206, '', None, None), nb.GOAL_BLOCKED)
        self.assertEqual(nb.fail_reason(208, '', 0, -1), nb.GOAL_UNKNOWN)
        self.assertEqual(nb.fail_reason(0, '', 0, nb.OUTSIDE), nb.GOAL_OUTSIDE)
        self.assertEqual(nb.fail_reason(204, '', None, None), nb.GOAL_OUTSIDE)

    def test_start_outside(self):
        self.assertEqual(nb.fail_reason(203, '', None, None), nb.START_OUTSIDE)
        self.assertEqual(nb.fail_reason(0, '', nb.OUTSIDE, 0), nb.START_OUTSIDE)

    def test_controller(self):
        self.assertEqual(nb.fail_reason(106, '', 0, 0), nb.COLLISION)
        self.assertEqual(nb.fail_reason(105, '', 99, 0), nb.COLLISION)   # 막힌 채 출발 = 충돌 위험
        self.assertEqual(nb.fail_reason(104, '', 0, 0), nb.NO_PROGRESS)
        self.assertEqual(nb.fail_reason(105, '', 0, 0), nb.NO_PROGRESS)
        self.assertEqual(nb.fail_reason(107, '', 0, 0), nb.NAV_TIMEOUT)
        self.assertIn('103', nb.fail_reason(103, 'bad path', 0, 0))

    def test_timeouts_tf_config(self):
        self.assertEqual(nb.fail_reason(207, '', 0, 0), nb.PLAN_TIMEOUT)
        self.assertEqual(nb.fail_reason(9003, '', 0, 0), nb.NAV_TIMEOUT)
        for code in (102, 202, 9002):
            self.assertEqual(nb.fail_reason(code, '', 99, 99), nb.TF_MISSING)
        for code in (101, 201, 9001):
            self.assertEqual(nb.fail_reason(code, '', 0, 0), nb.CONFIG)

    def test_unknown_code_keeps_message(self):
        r = nb.fail_reason(9000, 'something broke', 0, 0)
        self.assertIn('9000', r)
        self.assertIn('something broke', r)

    def test_unknown_costs(self):
        self.assertEqual(nb.fail_reason(208, '', None, None), nb.NO_PATH)


class Status(unittest.TestCase):
    def test_state_of(self):
        self.assertEqual(nb.state_of(nb.STATUS_SUCCEEDED), 'succeeded')
        self.assertEqual(nb.state_of(nb.STATUS_CANCELED), 'canceled')
        self.assertEqual(nb.state_of(nb.STATUS_ABORTED), 'failed')

    def test_contract_keys(self):
        d = json.loads(nb.make_status(3, 'running', (1.0, 2.0, 0.5), 1.234, 7.89, 1, 0, '', '', stamp=100.0))
        self.assertEqual(set(d), {'seq', 'state', 'goal', 'distance_remaining', 'eta_s', 'recoveries', 'error_code',
                                  'error_msg', 'reason', 'stamp'})
        self.assertEqual(d['seq'], 3)
        self.assertEqual(d['goal'], {'x': 1.0, 'y': 2.0, 'yaw': 0.5})
        self.assertEqual(d['distance_remaining'], 1.23)
        self.assertEqual(d['eta_s'], 7.9)
        self.assertEqual(d['stamp'], 100.0)

    def test_idle_nulls_and_korean(self):
        d = nb.make_status(0, 'idle')
        self.assertIn('"goal": null', d)
        self.assertIsNone(json.loads(d)['eta_s'])
        self.assertIn('출발점', nb.make_status(1, 'failed', reason=nb.START_BLOCKED))   # ensure_ascii=False


if __name__ == '__main__':
    unittest.main()
