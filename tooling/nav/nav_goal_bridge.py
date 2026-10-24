#!/usr/bin/env python3
"""대시보드 이동 목표를 NavigateToPose 액션으로 보내고 결과 · 실패 이유를 /nav/status 로 알린다 (PC nav 도메인).

bt_navigator 가 /goal_pose 를 직접 받으면 결과(error_code · error_msg)와 피드백이 아무에게도 가지 않아, 대시보드는
"보냄" 만 보이고 계획 실패를 모른다. 그래서 split_relay 가 로봇의 /goal_pose 를 여기서 /dashboard/goal_pose 로
바꿔 넘기고(nav_relay_topics.sh), 이 노드가 /navigate_to_pose 액션으로 보낸 뒤 상태를 /nav/status(JSON)로 낸다.
/nav/status 는 다시 로봇으로 중계된다. 계약(대시보드 · MCP 가 같이 쓴다 — 바꾸지 말 것):
  {"seq", "state": idle|planning|running|succeeded|failed|canceled, "goal": {"x","y","yaw"}|null,
   "distance_remaining", "eta_s", "recoveries", "error_code", "error_msg", "reason", "stamp"}
진행 중 2 Hz, 상태가 바뀌면 즉시, 끝난 뒤에는 5 s 마다 마지막 상태를 다시 낸다(늦게 붙은 대시보드도 보게).
새 목표가 오면 이전 목표는 Nav2 가 선점한다 — 이전 목표의 결과는 버린다.

reason 판단(fail_reason 등)은 ROS 없이 시험하는 순수 함수다: python -m unittest tooling/nav/test_nav_goal_bridge.py
"""
import json
import math
import time

BLOCKED = 99        # 전역 코스트맵(OccupancyGrid) 99 = 내접(팽창 안쪽), 100 = 장애물
UNKNOWN = -1        # 코스트맵의 미지 칸
OUTSIDE = 'outside'  # 칸이 코스트맵 밖

# action_msgs/GoalStatus
STATUS_SUCCEEDED, STATUS_CANCELED, STATUS_ABORTED = 4, 5, 6

START_BLOCKED = '출발점이 장애물에 막혀 있습니다 — 지도 초기화를 누르거나 로봇을 0.3 m 이상 빈 곳으로 옮기세요'
START_OUTSIDE = '로봇 위치가 지도 밖입니다 — 초기 위치(AMCL)를 다시 찍거나 지도 초기화를 누르세요'
GOAL_BLOCKED = '목표가 장애물(또는 그 가까이의 팽창 영역) 위에 있습니다 — 벽에서 떨어진 빈 곳을 다시 찍으세요'
GOAL_UNKNOWN = '목표가 지도에서 아직 모르는 곳입니다 — 탐색된(흰) 곳을 찍거나 그쪽을 먼저 둘러보세요'
GOAL_OUTSIDE = '목표가 지도 밖입니다 — 지도 안의 빈 곳을 찍으세요'
NO_PATH = '목표까지 갈 수 있는 길을 찾지 못했습니다 — 길이 막혔거나 통로가 좁습니다. 다른 곳을 찍으세요'
PLAN_TIMEOUT = '경로 계획 시간이 초과되었습니다 — 목표를 더 가깝게 찍거나 PC 부하를 확인하세요'
TF_MISSING = '로봇 위치(TF)를 모릅니다 — SLAM/AMCL 과 run_bridge.sh 중계(map→odom→base_link)가 도는지 확인하세요'
COLLISION = '충돌 위험으로 안전한 속도를 찾지 못했습니다 — 로봇 주변 장애물을 치우거나 로봇을 빈 곳으로 옮기세요'
NO_PROGRESS = '로봇이 앞으로 나아가지 못했습니다(장애물 · 바퀴 미끄러짐) — 주변을 비우고 다시 목표를 찍으세요'
NAV_TIMEOUT = '시간 안에 도착하지 못했습니다 — 경로가 막혔는지 보고 다시 목표를 찍으세요'
CONFIG = 'Nav2 설정 오류(플래너 · 컨트롤러 · 행동 트리)입니다 — PC 의 run_nav.sh 로그를 확인하세요'
CANCELED = '목표가 취소되었습니다(정지 · 비상 정지 · 취소)'
NO_SERVER = 'Nav2 가 실행 중이 아닙니다(/navigate_to_pose 없음) — PC 에서 tooling/nav/run_nav.sh 를 실행하세요'
REJECTED = 'Nav2 가 목표를 받지 않았습니다 — Nav2 가 완전히 뜰 때까지(wait_ready.sh) 기다린 뒤 다시 찍으세요'


def yaw_of(q):
    return math.atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z))


def cell_cost(grid, x, y):
    """grid = {"resolution", "width", "height", "origin": (x, y, yaw), "data"} 에서 (x, y) 칸 값. 밖이면 OUTSIDE."""
    ox, oy, oyaw = grid['origin']
    dx, dy = x - ox, y - oy
    c, s = math.cos(-oyaw), math.sin(-oyaw)
    i = math.floor((c * dx - s * dy) / grid['resolution'])
    j = math.floor((s * dx + c * dy) / grid['resolution'])
    if not (0 <= i < grid['width'] and 0 <= j < grid['height']):
        return OUTSIDE
    return grid['data'][j * grid['width'] + i]


def blocked(cost):
    return cost is not None and cost != OUTSIDE and cost >= BLOCKED


def fail_reason(error_code, error_msg='', start_cost=None, goal_cost=None):
    """Nav2 실패의 한국어 한 줄 이유. start_cost · goal_cost 는 전역 코스트맵 값(None = 모름, OUTSIDE = 밖).

    error_code 는 실패한 BT 노드의 것이다: 1xx FollowPath, 2xx ComputePathToPose, 9xxx NavigateToPose.
    계획 실패는 Nav2 가 '경로 없음(208)' 처럼 뭉뚱그려 낼 때가 많아 코스트맵 값으로 출발점 · 목표를 먼저 본다.
    """
    if error_code in (102, 202, 9002):
        return TF_MISSING
    if error_code in (101, 201, 9001):
        return CONFIG
    if 100 <= error_code < 200:   # 컨트롤러: 출발점이 막힌 채 시작하면 계획은 됐어도 바로 여기로 온다
        if error_code == 106 or blocked(start_cost):
            return COLLISION
        if error_code in (104, 105):
            return NO_PROGRESS
        if error_code == 107:
            return NAV_TIMEOUT
        return f'경로를 따라가지 못했습니다(error_code {error_code}{": " + error_msg if error_msg else ""}) — 다시 목표를 찍으세요'
    if error_code == 205 or blocked(start_cost):
        return START_BLOCKED
    if error_code == 203 or start_cost == OUTSIDE:
        return START_OUTSIDE
    if error_code == 206 or blocked(goal_cost):
        return GOAL_BLOCKED
    if goal_cost == UNKNOWN:
        return GOAL_UNKNOWN
    if error_code == 204 or goal_cost == OUTSIDE:
        return GOAL_OUTSIDE
    if error_code == 208:
        return NO_PATH
    if error_code == 207:
        return PLAN_TIMEOUT
    if error_code == 9003:
        return NAV_TIMEOUT
    detail = f'error_code {error_code}' + (f': {error_msg}' if error_msg else '')
    return f'Nav2 가 목표를 끝내지 못했습니다({detail}) — /plan 과 코스트맵을 확인하고 다시 찍으세요'


def state_of(status):
    return {STATUS_SUCCEEDED: 'succeeded', STATUS_CANCELED: 'canceled'}.get(status, 'failed')


def make_status(seq, state, goal=None, distance=None, eta=None, recoveries=0, error_code=0, error_msg='', reason='',
                stamp=None):
    """/nav/status 의 JSON 문자열. goal = (x, y, yaw) 또는 None."""
    return json.dumps({
        'seq': seq, 'state': state,
        'goal': None if goal is None else {'x': round(goal[0], 3), 'y': round(goal[1], 3), 'yaw': round(goal[2], 3)},
        'distance_remaining': None if distance is None else round(distance, 2),
        'eta_s': None if eta is None else round(eta, 1),
        'recoveries': recoveries, 'error_code': error_code, 'error_msg': error_msg, 'reason': reason,
        'stamp': round(time.time() if stamp is None else stamp, 3)}, ensure_ascii=False)


ACTIVE = ('planning', 'running')


def main():
    import rclpy
    from geometry_msgs.msg import PoseStamped
    from nav2_msgs.action import NavigateToPose
    from nav_msgs.msg import OccupancyGrid
    from rclpy.action import ActionClient
    from rclpy.duration import Duration
    from rclpy.qos import DurabilityPolicy, QoSProfile
    from rclpy.time import Time
    from std_msgs.msg import String
    from tf2_ros import Buffer, TransformException, TransformListener

    rclpy.init()
    node = rclpy.create_node('nav_goal_bridge')
    buf = Buffer()
    TransformListener(buf, node)
    client = ActionClient(node, NavigateToPose, '/navigate_to_pose')
    pub = node.create_publisher(String, '/nav/status', 10)
    s = {'seq': 0, 'state': 'idle', 'goal': None, 'distance': None, 'eta': None, 'recoveries': 0,
         'error_code': 0, 'error_msg': '', 'reason': '', 'goal_msg': None, 'last_pub': 0.0}
    grid = {}

    def publish():
        pub.publish(String(data=make_status(s['seq'], s['state'], s['goal'], s['distance'], s['eta'], s['recoveries'],
                                            s['error_code'], s['error_msg'], s['reason'])))
        s['last_pub'] = time.monotonic()

    def set_state(state, reason=''):
        s['state'], s['reason'] = state, reason
        node.get_logger().info(f'goal {s["seq"]}: {state}{" — " + reason if reason else ""}')
        publish()

    def on_costmap(m):
        o = m.info.origin
        grid.update(resolution=m.info.resolution, width=m.info.width, height=m.info.height, data=m.data,
                    frame=m.header.frame_id, origin=(o.position.x, o.position.y, yaw_of(o.orientation)))

    def pose_in_grid(frame, x, y):
        """frame 의 (x, y) 를 코스트맵 프레임으로. 변환을 모르면 None."""
        if frame == grid['frame']:
            return x, y
        try:
            t = buf.lookup_transform(grid['frame'], frame, Time(), timeout=Duration(seconds=0)).transform
        except TransformException:
            return None
        a = yaw_of(t.rotation)
        return (t.translation.x + math.cos(a) * x - math.sin(a) * y,
                t.translation.y + math.sin(a) * x + math.cos(a) * y)

    def costs():
        """(출발점 값, 목표 값) — 코스트맵 · 위치를 모르면 None."""
        if not grid:
            return None, None
        start = pose_in_grid('base_link', 0.0, 0.0)
        g = s['goal_msg']
        goal = pose_in_grid(g.header.frame_id or grid['frame'], g.pose.position.x, g.pose.position.y)
        return (None if start is None else cell_cost(grid, *start)), (None if goal is None else cell_cost(grid, *goal))

    def on_goal(m):
        s['seq'] += 1
        seq = s['seq']
        s.update(goal=(m.pose.position.x, m.pose.position.y, yaw_of(m.pose.orientation)), distance=None, eta=None,
                 recoveries=0, error_code=0, error_msg='', goal_msg=m)
        if not client.server_is_ready():
            return set_state('failed', NO_SERVER)
        set_state('planning')
        fut = client.send_goal_async(NavigateToPose.Goal(pose=m), feedback_callback=lambda fb: on_feedback(seq, fb))
        fut.add_done_callback(lambda f: on_accepted(seq, f))

    def on_accepted(seq, fut):
        if seq != s['seq']:
            return
        handle = fut.result()
        if handle is None or not handle.accepted:
            return set_state('failed', REJECTED)
        handle.get_result_async().add_done_callback(lambda f: on_result(seq, f))

    def on_feedback(seq, fb):
        if seq != s['seq'] or s['state'] not in ACTIVE:
            return
        f = fb.feedback
        eta = f.estimated_time_remaining.sec + f.estimated_time_remaining.nanosec * 1e-9
        s.update(distance=f.distance_remaining, eta=eta if eta > 0 else None, recoveries=f.number_of_recoveries)
        # 경로가 나오기 전 피드백은 남은 거리가 0 이다 — 0 보다 커지면 계획이 끝나 따라가는 중
        if s['state'] == 'planning' and f.distance_remaining > 0:
            set_state('running')

    def on_result(seq, fut):
        if seq != s['seq']:   # 새 목표에 선점된 이전 목표
            return
        r = fut.result()
        state = state_of(r.status)
        # error_msg 는 Jazzy 부터 있다
        s.update(error_code=int(getattr(r.result, 'error_code', 0)), error_msg=getattr(r.result, 'error_msg', ''))
        if state == 'succeeded':
            s['distance'], s['eta'] = 0.0, None
            return set_state(state)
        if state == 'canceled':
            return set_state(state, CANCELED)
        start, goal = costs()
        set_state(state, fail_reason(s['error_code'], s['error_msg'], start, goal))

    def tick():
        # 진행 중 2 Hz, 끝난 뒤(idle 포함)는 5 s 마다 마지막 상태
        if s['state'] in ACTIVE or time.monotonic() - s['last_pub'] >= 5.0:
            publish()

    node.create_subscription(PoseStamped, '/dashboard/goal_pose', on_goal, 10)
    node.create_subscription(OccupancyGrid, '/global_costmap/costmap', on_costmap,
                             QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
    node.create_timer(0.5, tick)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
