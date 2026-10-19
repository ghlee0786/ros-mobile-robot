"""가짜 대시보드: run_scenarios.py 의 판정 규칙을 실제 앱 없이 시험한다.

대시보드 시험 훅 인자(--url --actions --state --log --quit-after ...)를 받아, 실제 대시보드처럼 advertise 4 개와
같은 throttle 의 subscribe 를 보내고 actions.json 의 robot.drive · robot.estop · robot.straight 를 흉내 낸다.
끝나면 state.json 을 쓰고 --fake-exit 코드로 끝난다. 시험용 덧붙임 인자:
  --fake-state JSON   state.json 에 덮어쓸 값(깊은 병합)
  --fake-log 줄       dash.log 에 쓸 줄(여러 번)
  --fake-exit N       종료 코드
"""
import argparse
import json
import math
import sys
import time

import websocket

SUBS = [("/scan", "sensor_msgs/LaserScan", 0), ("/imu/data_raw", "sensor_msgs/Imu", 50),
        ("/odom", "nav_msgs/Odometry", 0), ("/wheel_states", "sensor_msgs/JointState", 0),
        ("/wheel_cmd", "sensor_msgs/JointState", 0), ("/tf", "tf2_msgs/TFMessage", 200),
        ("/tf_static", "tf2_msgs/TFMessage", 0), ("/link/heartbeat", "std_msgs/String", 0),
        ("/straight_drive/status", "std_msgs/String", 0), ("/plan", "nav_msgs/Path", 0)]
ADV = [("/teleop/cmd_vel", "geometry_msgs/Twist"), ("/straight_drive/goal", "geometry_msgs/Vector3"),
       ("/straight_drive/cancel", "std_msgs/Empty"), ("/goal_pose", "geometry_msgs/PoseStamped")]


def merge(a, b):
    for k, v in b.items():
        if isinstance(v, dict) and isinstance(a.get(k), dict):
            merge(a[k], v)
        else:
            a[k] = v
    return a


def twist(v, w):
    return {"linear": {"x": v, "y": 0, "z": 0}, "angular": {"x": 0, "y": 0, "z": w}}


def main():
    p = argparse.ArgumentParser()
    for name in ("--url", "--settings", "--size", "--actions", "--screenshot", "--state", "--log"):
        p.add_argument(name)
    p.add_argument("--quit-after", type=float, default=5)
    p.add_argument("--no-model", action="store_true")
    p.add_argument("--no-publish", action="store_true")
    p.add_argument("--fake-state", default="{}")
    p.add_argument("--fake-log", action="append", default=[])
    p.add_argument("--fake-exit", type=int, default=0)
    a = p.parse_args()

    actions = []
    if a.actions:
        with open(a.actions, encoding="utf-8") as f:
            actions = sorted(json.load(f), key=lambda x: x["t"])
    ws = None
    try:
        ws = websocket.create_connection(a.url, timeout=3)
    except (OSError, websocket.WebSocketException):
        pass

    def send(o):
        if ws:
            ws.send(json.dumps(o))

    def pub(topic, msg):
        if not a.no_publish:
            send({"op": "publish", "topic": topic, "msg": msg})

    if not a.no_publish:
        for t, ty in ADV:
            send({"op": "advertise", "topic": t, "type": ty})
    for t, ty, th in SUBS:
        sub = {"op": "subscribe", "topic": t, "type": ty}
        if th:
            sub.update(throttle_rate=th, queue_length=1)
        send(sub)

    robot = {"distance": 0.0, "trail": {"count": 0}, "mode": "키보드 조종", "straightResult": {}}
    seen, done = set(), []
    last_xy, lin, ang, keyboard = None, 0.0, 0.0, True
    t0 = time.perf_counter()
    next_teleop = 0.0
    while (el := time.perf_counter() - t0) < a.quit_after:
        while actions and actions[0]["t"] <= el:
            act = actions.pop(0)
            done.append({**act, "wall": time.time()})
            call, args = act.get("call"), act.get("args", [])
            if call == "robot.drive":
                lin, ang = args[0], args[1]
                pub("/teleop/cmd_vel", twist(lin * 0.1, ang * 0.25))
            elif call == "robot.estop":
                lin = ang = 0.0
                for _ in range(3):
                    pub("/teleop/cmd_vel", twist(0, 0))
                pub("/straight_drive/cancel", {})
                send({"op": "call_service", "service": "/navigate_to_pose/_action/cancel_goal", "args": {}, "id": "c1"})
                keyboard = False
            elif call == "robot.straight":
                pub("/straight_drive/goal", {"x": args[0], "y": 0.1, "z": 0})
        if keyboard and el >= next_teleop:
            pub("/teleop/cmd_vel", twist(lin * 0.1, ang * 0.25))
            next_teleop = el + 0.1
        if ws:
            ws.settimeout(0.02)
            try:
                m = json.loads(ws.recv())
            except (websocket.WebSocketTimeoutException, ValueError):
                continue
            except (OSError, websocket.WebSocketException):
                ws = None
                continue
            topic = m.get("topic")
            seen.add(topic)
            if topic == "/odom":
                pp = m["msg"]["pose"]["pose"]["position"]
                if last_xy:
                    robot["distance"] += math.hypot(pp["x"] - last_xy[0], pp["y"] - last_xy[1])
                    if math.hypot(pp["x"] - last_xy[0], pp["y"] - last_xy[1]) > 0:
                        robot["trail"]["count"] += 1
                last_xy = (pp["x"], pp["y"])
            elif topic == "/straight_drive/status":
                st = json.loads(m["msg"]["data"])
                if st["state"] == "finished":
                    robot["straightResult"] = st
        else:
            time.sleep(0.02)
    robot["missingRequired"] = [t for t in ("/odom", "/scan", "/wheel_states") if t not in seen]
    state = {"win": {"page": 0}, "bridge": {"connected": ws is not None}, "robot": robot, "scene": {},
             "warnings": [], "actions_done": done}
    merge(state, json.loads(a.fake_state))
    if a.state:
        with open(a.state, "w", encoding="utf-8") as f:
            json.dump(state, f, ensure_ascii=False)
    if a.log:
        with open(a.log, "w", encoding="utf-8") as f:
            f.write("\n".join(a.fake_log) + "\n")
    if ws:
        ws.close()
    return a.fake_exit


if __name__ == "__main__":
    sys.exit(main())
