"""시나리오 실행기 계약: 판정 규칙 계산, fake_dashboard 로 끝까지 실행, --list, --no-dashboard."""
import json
import os
import subprocess
import sys
import threading
import time

import run_scenarios as rs

from conftest import HERE, ROOT, Client

FAKE = os.path.join(HERE, "fake_dashboard.py")


def rx(op, t, wall=None, **kw):
    return {"dir": "rx", "op": op, "t": t, "wall": wall if wall is not None else 1000 + t, "conn": kw.pop("conn", 1), **kw}


def test_state_rules():
    st = {"robot": {"missingRequired": [], "trail": [1, 2, 3], "mapSize": {"width": 100.0}, "x": 0.5},
          "bridge": {"connected": True}}
    ok = [{"path": "robot.missingRequired", "op": "==", "value": []}, {"path": "bridge.connected", "op": "==", "value": True},
          {"path": "robot.trail.count", "op": ">", "value": 2}, {"path": "robot.mapSize.width", "op": ">=", "value": 100},
          {"path": "robot.x", "op": "abs<=", "value": 0.5}]
    assert rs.check_state(st, ok) == []
    bad = rs.check_state(st, [{"path": "bridge.connected", "op": "==", "value": False},
                              {"path": "robot.nope", "op": "==", "value": 1}, {"path": "robot.x", "op": "<", "value": 0.1}])
    assert len(bad) == 3


def test_trace_rules():
    tr = [{"t": 1, "robot": {"linkLost": False}}, {"t": 9, "robot": {"linkLost": True}}, {"t": 14, "robot": {"linkLost": False}}]
    good = [{"rule": "was_true_between", "path": "robot.linkLost", "value": True, "from_s": 8, "to_s": 12},
            {"rule": "final_eq", "path": "robot.linkLost", "value": False},
            {"rule": "never_between", "path": "robot.linkLost", "value": True, "from_s": 10, "to_s": 20}]
    assert rs.check_trace(tr, good) == []
    bad = [{"rule": "was_true_between", "path": "robot.linkLost", "value": True, "from_s": 0, "to_s": 5},
           {"rule": "final_eq", "path": "robot.linkLost", "value": True},
           {"rule": "never_between", "path": "robot.linkLost", "value": True, "from_s": 0, "to_s": 20}]
    assert len(rs.check_trace(tr, bad)) == 3


def test_server_log_rules():
    def tw(v, w=0.0):
        return {"linear": {"x": v}, "angular": {"z": w}}
    recs = [{"dir": "event", "op": "connect", "t": 0.0, "wall": 1000.0, "conn": 1},
            rx("advertise", 0.1, topic="/teleop/cmd_vel"), rx("subscribe", 0.1, topic="/imu/data_raw", throttle_rate=50),
            rx("subscribe", 0.1, topic="/scan")]
    recs += [rx("publish", 1 + i * 0.1, topic="/teleop/cmd_vel", msg=tw(0.1)) for i in range(40)]   # 1..5 s 10 Hz
    recs += [rx("publish", 5.02 + i * 0.01, topic="/teleop/cmd_vel", msg=tw(0.0)) for i in range(3)]
    recs += [rx("publish", 5.05, topic="/straight_drive/cancel", msg={}),
             rx("call_service", 5.06, service="/navigate_to_pose/_action/cancel_goal")]
    done = [{"t": 5, "call": "robot.estop", "wall": 1005.0}]
    good = [{"rule": "max_abs", "topic": "/teleop/cmd_vel", "field": "linear.x", "value": 0.1},
            {"rule": "count_ge", "op": "subscribe", "topic": "/scan", "value": 1},
            {"rule": "count_le", "op": "subscribe", "topic": "/camera/image_raw", "value": 0},
            {"rule": "throttle_eq", "topic": "/imu/data_raw", "value": 50},
            {"rule": "rate_between", "topic": "/teleop/cmd_vel", "from_s": 1, "to_s": 4.95, "min_hz": 9, "max_hz": 11},
            {"rule": "within_ms_after_action", "action": "robot.estop", "topic": "/teleop/cmd_vel",
             "where": {"linear.x": 0}, "ms": 100, "min_count": 3},
            {"rule": "within_ms_after_action", "action": "robot.estop", "op": "call_service",
             "service": "/navigate_to_pose/_action/cancel_goal", "ms": 100},
            {"rule": "zero_after_action", "action": "robot.estop", "topic": "/teleop/cmd_vel", "fields": ["linear.x"]}]
    assert rs.check_server_log(recs, good, done) == []
    bad = [{"rule": "max_abs", "topic": "/teleop/cmd_vel", "field": "linear.x", "value": 0.05},
           {"rule": "count_ge", "op": "subscribe", "topic": "/scan", "value": 2},
           {"rule": "throttle_eq", "topic": "/imu/data_raw", "value": 0},
           {"rule": "rate_between", "topic": "/teleop/cmd_vel", "from_s": 1, "to_s": 5, "min_hz": 20, "max_hz": 30},
           {"rule": "within_ms_after_action", "action": "robot.estop", "topic": "/teleop/cmd_vel",
            "where": {"linear.x": 0}, "ms": 20, "min_count": 3},
           {"rule": "zero_after_action", "action": "robot.estop", "topic": "/teleop/cmd_vel", "fields": ["linear.x"],
            "after_ms": -2000}]
    assert len(rs.check_server_log(recs, bad, done)) == 6
    # 액션이 actions_done 에 없으면 실패로 센다
    assert rs.check_server_log(recs, good[5:6], []) != []


def test_resubscribe_and_goal_rules():
    def session(conn, t0, topics, adv=4):
        out = [{"dir": "event", "op": "connect", "t": t0, "wall": 1000 + t0, "conn": conn}]
        out += [rx("advertise", t0 + 0.1, conn=conn, topic=f"/a{i}") for i in range(adv)]
        out += [rx("subscribe", t0 + 0.2, conn=conn, topic=t) for t in topics]
        return out
    recs = session(1, 0, ["/odom", "/scan"]) + session(2, 10, ["/scan", "/odom"]) + session(3, 20, ["/odom", "/scan"])
    rule = [{"rule": "resubscribe_within", "within_s": 4, "min_reconnects": 2, "min_advertise": 4}]
    assert rs.check_server_log(recs, rule) == []
    recs2 = session(1, 0, ["/odom", "/scan"]) + session(2, 10, ["/odom"], adv=2)
    assert rs.check_server_log(recs2, [dict(rule[0], min_reconnects=1)]) != []
    # map→odom (1,2,30°) 에서 odom (0.5,0.2,0.3) 을 map 으로 보낸 /goal_pose
    import math
    c, s = math.cos(math.radians(30)), math.sin(math.radians(30))
    x, y, yaw = 1 + c * 0.5 - s * 0.2, 2 + s * 0.5 + c * 0.2, 0.3 + math.radians(30)
    goal = rx("publish", 3, topic="/goal_pose", msg={"header": {"frame_id": "map"}, "pose": {
        "position": {"x": x, "y": y, "z": 0}, "orientation": {"x": 0, "y": 0, "z": math.sin(yaw / 2), "w": math.cos(yaw / 2)}}})
    g = {"rule": "goal_inverse", "frame": "map", "map_odom": [1, 2, 30], "odom": [0.5, 0.2, 0.3]}
    assert rs.check_server_log([goal], [g]) == []
    assert rs.check_server_log([goal], [dict(g, odom=[0.55, 0.2, 0.3])]) != []


def test_dash_log_rules():
    text = "ok\nframe gui_ms=12.5\nframe gui_ms=80\nqml: TypeError: x"
    assert rs.check_dash_log(text, {"log_forbid": ["ReferenceError"], "log_regex_max": [{"regex": r"gui_ms=([0-9.]+)", "max": 100}]}) == []
    assert len(rs.check_dash_log(text, {"log_forbid": ["TypeError"], "log_regex_max": [{"regex": r"gui_ms=([0-9.]+)", "max": 50}]})) == 2


def write(d, sc):
    with open(os.path.join(d, sc["id"] + ".json"), "w", encoding="utf-8") as f:
        json.dump(sc, f, ensure_ascii=False)


def test_run_with_fake_dashboard(tmp_path):
    scd = tmp_path / "sc"
    scd.mkdir()
    write(scd, {"id": "good", "duration_s": 4, "server_args": [], "actions": [
        {"t": 1, "call": "robot.drive", "args": [1, 0]}, {"t": 2.5, "call": "robot.estop", "args": []}],
        "expect": {"state": [{"path": "bridge.connected", "op": "==", "value": True},
                             {"path": "robot.missingRequired", "op": "==", "value": []},
                             {"path": "robot.distance", "op": ">", "value": 0.03}],
                   "server_log": [{"rule": "throttle_eq", "topic": "/imu/data_raw", "value": 50, "all": True},
                                  {"rule": "count_ge", "op": "advertise", "value": 4},
                                  {"rule": "max_abs", "topic": "/teleop/cmd_vel", "field": "linear.x", "value": 0.1},
                                  {"rule": "within_ms_after_action", "action": "robot.estop", "topic": "/teleop/cmd_vel",
                                   "where": {"linear.x": 0}, "ms": 100, "min_count": 3},
                                  {"rule": "zero_after_action", "action": "robot.estop", "topic": "/teleop/cmd_vel",
                                   "fields": ["linear.x", "angular.z"]}],
                   "log_forbid": ["TypeError"]}})
    write(scd, {"id": "bad", "duration_s": 2, "server_args": [], "actions": [],
                "dashboard_args": ["--fake-exit", "3", "--fake-log", "qml: TypeError: boom",
                                   "--fake-state", json.dumps({"robot": {"linkLost": True}})],
                "expect": {"state": [{"path": "robot.linkLost", "op": "==", "value": False}], "log_forbid": ["TypeError"]}})
    write(scd, {"id": "restart", "duration_s": 5, "server_args": [], "dashboards": 0,
                "actions": [{"t": 1, "runner": "restart_server", "down_s": 0.3}], "expect": {}})
    write(scd, {"id": "pi_x", "needs_pi": True, "server": False, "duration_s": 1, "expect": {}})
    write(scd, {"id": "later", "requires": ["P4-core"], "duration_s": 1, "expect": {}})
    out = tmp_path / "out"
    rc = rs.main(["--scenarios", str(scd), "--out", str(out), "--offline", "--skip-requires", "--jobs", "3",
                  "--dashboard-cmd", f'"{sys.executable}" "{FAKE}"'])
    summary = {r["id"]: r for r in json.load(open(out / "summary.json", encoding="utf-8"))["results"]}
    assert summary["good"]["status"] == "pass", summary["good"]
    assert summary["restart"]["status"] == "pass", summary["restart"]
    assert summary["bad"]["status"] == "fail"
    f = " ".join(summary["bad"]["failures"])
    assert "종료 코드 3" in f and "TypeError" in f and "linkLost" in f
    assert summary["pi_x"]["status"] == "skip" and summary["later"]["status"] == "skip"
    assert rc == 1
    cmd = (out / "good" / "cmd.txt").read_text(encoding="utf-8")
    for part in ("--url ws://127.0.0.1:", "--settings", "--size 1440x900", "--actions", "--screenshot", "--state",
                 "--log", "--quit-after 4", "--no-model"):
        assert part in cmd
    log = rs.read_log(str(out / "restart" / "server.jsonl"))
    assert sum(1 for r in log if r.get("op") == "connect") == 0     # 대시보드 없음, 서버 재시작만


def test_list_real_scenarios():
    r = subprocess.run([sys.executable, os.path.join(ROOT, "run_scenarios.py"), "--list"], capture_output=True,
                       text=True, encoding="utf-8")
    assert r.returncode == 0
    ids = {line.split()[0] for line in r.stdout.splitlines() if line.strip()}
    want = {"normal", "reconnect", "halfopen", "freeze", "bigmap", "mapflip", "flood", "estop", "goal", "watchdog_mock",
            "straight", "record_throttle", "replay", "wifi_bad", "skew", "bad_json", "two_clients", "soak", "no_rosapi",
            "pi_normal"}
    assert want <= ids
    for sc in rs.load_scenarios(os.path.join(ROOT, "scenarios")):
        assert sc["id"] and sc.get("duration_s") and "expect" in sc
        if sc.get("needs_pi"):
            assert sc.get("server") is False and "--no-publish" in sc["dashboard_args"]


def test_no_dashboard_server_accepts_pingpong(tmp_path):
    """--no-dashboard 로 서버만 띄워 다른 클라이언트(핑퐁 흐름)로 붙는다."""
    scd = tmp_path / "sc"
    scd.mkdir()
    write(scd, {"id": "srv", "duration_s": 6, "server_args": [], "expect": {}})
    out = tmp_path / "out"
    th = threading.Thread(target=rs.main, args=(["--scenarios", str(scd), "--out", str(out), "--no-dashboard"],))
    th.start()
    try:
        port = None
        for _ in range(100):
            p = out / "srv" / "server.out"
            if p.exists():
                text = p.read_text(encoding="utf-8")
                if "에서 대기" in text:
                    port = int(text.split("ws://127.0.0.1:")[1].split()[0])
                    break
            time.sleep(0.1)
        assert port
        c = Client(port)
        c.send(op="advertise", topic="/link/ping", type="std_msgs/String")
        c.send(op="subscribe", topic="/link/pong", type="std_msgs/String")
        time.sleep(0.2)
        c.send(op="publish", topic="/link/ping", msg={"data": "tok"})
        assert c.until(lambda m: m.get("topic") == "/link/pong" and m["msg"]["data"].startswith("tok|pi="))
        c.close()
    finally:
        th.join(20)
    assert json.load(open(out / "summary.json", encoding="utf-8"))["results"][0]["status"] == "pass"
