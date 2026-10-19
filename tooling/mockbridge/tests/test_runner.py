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


def test_zero_after_action_same_batch():
    """20261020_042326 estop: 직전 10 Hz 명령(0.08)이 estop 의 0 명령들과 한 묶음으로 와 wall 이 estop 보다 0.6 ms 늦었다."""
    def tw(v):
        return {"linear": {"x": v}, "angular": {"z": 0.0}}
    rule = [{"rule": "zero_after_action", "action": "robot.estop", "topic": "/teleop/cmd_vel", "fields": ["linear.x"]}]
    done = [{"call": "robot.estop", "wall": 1005.0}]
    batch = [rx("publish", 4.9, topic="/teleop/cmd_vel", msg=tw(0.08)),
             rx("publish", 5.0006, topic="/teleop/cmd_vel", msg=tw(0.08))]
    batch += [rx("publish", 5.0006, topic="/teleop/cmd_vel", msg=tw(0.0)) for _ in range(3)]
    assert rs.check_server_log(batch, rule, done) == []
    late = batch + [rx("publish", 5.1, topic="/teleop/cmd_vel", msg=tw(0.08))]   # 0 명령 뒤의 비0 은 여전히 실패
    assert rs.check_server_log(late, rule, done) != []


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


def test_mem_growth_skips_warmup():
    # 20261020_024216 soak 실측: 0 s(QML 로드 전) 51 MB → 10 s 307 MB → 190 s 뒤 195±3 MB 평탄. 예전 판정은 144 MB '증가'.
    soak = [(0.0, 51.0), (10.1, 307.2), (60.4, 308.6), (120.7, 247.1), (181.0, 211.4), (300.0, 197.9), (603.4, 194.8)]
    assert rs.mem_growth(soak, 30) < 0
    leak = [(t, 200.0 + t * 0.2) for t in range(0, 601, 10)]   # 분당 12 MB 씩 느는 진짜 누수
    assert rs.mem_growth(leak, 30) > 50
    assert rs.mem_growth([(0, 1.0), (40, 2.0)], 30) is None


def test_rebase_trace_and_missing_op():
    tr = [{"t": 0.5, "wall": 1003.0, "robot": {"linkLost": False}}, {"t": 6.0, "wall": 1008.5, "robot": {"linkLost": True}}]
    rule = [{"rule": "was_true_between", "path": "robot.linkLost", "value": True, "from_s": 8, "to_s": 12}]
    assert rs.check_trace(tr, rule) != []                       # 대시보드 시계(로드 뒤 6 s)로는 창 밖
    assert rs.check_trace(rs.rebase_trace(tr, 1000.0), rule) == []   # 서버 시계로는 8.5 s
    st = {"robot": {"straightResult": {"reason": "reached"}}}
    assert rs.check_state(st, [{"path": "robot.straightResult.heading", "op": "missing"}]) == []
    assert rs.check_state(st, [{"path": "robot.straightResult.reason", "op": "missing"}]) != []


def test_shot_path_and_trace_arg(tmp_path):
    scd = tmp_path / "sc"
    scd.mkdir()
    write(scd, {"id": "shot", "duration_s": 2, "server_args": [], "actions": [{"t": 1, "shot": "a.png"}],
                "expect": {"trace": [{"rule": "final_eq", "path": "x", "value": None}]}})
    out = tmp_path / "out"
    rs.main(["--scenarios", str(scd), "--out", str(out), "--dashboard-cmd", f'"{sys.executable}" "{FAKE}"'])
    acts = json.load(open(out / "shot" / "actions.json", encoding="utf-8"))
    assert acts[0]["shot"] == str(out / "shot" / "a.png")
    assert "--trace " + str(out / "shot" / "trace.jsonl") in (out / "shot" / "cmd.txt").read_text(encoding="utf-8")


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


def test_server_start_failure_keeps_summary(tmp_path):
    """B20: 서버가 뜨지 않는 시나리오가 있어도 그것만 '실행 오류' 로 불합격하고 나머지 결과 · 요약을 낸다."""
    scd = tmp_path / "sc"
    scd.mkdir()
    write(scd, {"id": "badsrv", "duration_s": 2, "server_args": ["--no-such-flag"], "expect": {"mem_growth_max_mb": 50}})
    write(scd, {"id": "ok", "duration_s": 2, "server_args": [], "expect": {}})
    out = tmp_path / "out"
    rc = rs.main(["--scenarios", str(scd), "--out", str(out), "--jobs", "2",
                  "--dashboard-cmd", f'"{sys.executable}" "{FAKE}"'])
    summary = {r["id"]: r for r in json.load(open(out / "summary.json", encoding="utf-8"))["results"]}
    assert summary["badsrv"]["status"] == "fail" and "실행 오류" in " ".join(summary["badsrv"]["failures"])
    assert summary["ok"]["status"] == "pass", summary["ok"]
    assert rc == 1


def test_out_in_actions(tmp_path):
    """B33: actions 안의 {out} · {here} 도 바꿔 넣는다(shot 이 '{out}/mid.png' 그대로 가면 대시보드가 저장에 실패한다)."""
    scd = tmp_path / "sc"
    scd.mkdir()
    write(scd, {"id": "acts", "duration_s": 2, "server_args": [], "dashboards": 2,
                "actions": [{"t": 1, "shot": "{out}/mid.png"}, {"t": 1, "call": "robot.f", "args": ["{here}/x", 1]}],
                "expect": {}})
    out = tmp_path / "out"
    rs.main(["--scenarios", str(scd), "--out", str(out), "--dashboard-cmd", f'"{sys.executable}" "{FAKE}"'])
    for d in (out / "acts", out / "acts" / "dash1"):
        acts = json.load(open(d / "actions.json", encoding="utf-8"))
        assert os.path.normpath(acts[0]["shot"]) == os.path.join(str(d), "mid.png")
        assert os.path.normpath(acts[1]["args"][0]) == os.path.join(rs.HERE, "x") and acts[1]["args"][1] == 1


def test_stale_exe(tmp_path, capsys):
    """B19: 대시보드 exe 가 소스보다 오래됐으면 실행기가 돌지 않고 종료 코드 2 로 알린다(trace wall 이 없던 04:01 빌드)."""
    root = tmp_path / "dashboard"
    (root / "build").mkdir(parents=True)
    (root / "src" / "tools").mkdir(parents=True)
    exe = root / "build" / "robot_dashboard.exe"
    exe.write_text("x")
    src = root / "src" / "tools" / "a.cpp"
    src.write_text("x")
    now = time.time()
    os.utime(exe, (now - 100, now - 100))
    os.utime(src, (now, now))
    assert rs.stale_exe(str(exe)) == str(src)
    scd = tmp_path / "sc"
    scd.mkdir()
    write(scd, {"id": "x", "duration_s": 1, "expect": {}})
    assert rs.main(["--scenarios", str(scd), "--out", str(tmp_path / "out"), "--exe", str(exe)]) == 2
    assert "오래됨" in capsys.readouterr().err
    os.utime(exe, (now + 10, now + 10))
    assert rs.stale_exe(str(exe)) is None
