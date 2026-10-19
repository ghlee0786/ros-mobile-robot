"""rosbridge 프로토콜 계약: 핑퐁 흐름 · throttle · unsubscribe · rosapi · Foxy 의 무응답 동작 · inf→null."""
import json
import os
import random
import subprocess
import sys
import time
import types

import websocket

from conftest import ROOT, run_scenarios
from mockbridge.protocol import Conn, Packet, Sub, encode

PINGPONG = os.path.normpath(os.path.join(ROOT, "..", "pc", "rosbridge_pingpong_test.py"))


def test_pingpong_flow(mock, client):
    """rosbridge_pingpong_test.py 와 같은 흐름(포트만 다르다)."""
    c = client(mock().port)
    c.send(op="advertise", topic="/link/ping", type="std_msgs/String")
    c.send(op="subscribe", topic="/link/pong", type="std_msgs/String")
    time.sleep(0.3)
    for i in range(3):
        token = f"pc-rosbridge-{i}-{time.time():.6f}"
        c.send(op="publish", topic="/link/ping", msg={"data": token})
        reply = c.until(lambda m: m.get("topic") == "/link/pong" and m["msg"]["data"].startswith(token), 2.0)
        assert reply and "|pi=" in reply["msg"]["data"] and "|pi_time=" in reply["msg"]["data"]
    topics = c.call("/rosapi/topics")
    assert topics and set(topics["values"]["topics"]) >= {"/link/heartbeat", "/odom", "/scan", "/imu/data_raw"}
    for topic in ["/link/heartbeat", "/odom", "/scan", "/imu/data_raw"]:
        c.send(op="subscribe", topic=topic, throttle_rate=200)
        assert c.until(lambda m, t=topic: m.get("topic") == t, 3.0), topic
        c.send(op="unsubscribe", topic=topic)


# 원래 스크립트는 고치지 않고, websocket.create_connection 만 바꿔 9090 을 시험 포트로 돌린다.
REDIRECT = """
import runpy, sys, websocket
port, script = sys.argv[1], sys.argv[2]
orig = websocket.create_connection
websocket.create_connection = lambda url, **kw: orig(url.replace(":9090", ":" + port), **kw)
sys.argv = [script] + sys.argv[3:]
runpy.run_path(script, run_name="__main__")
"""


def test_pingpong_script(mock):
    """tooling/pc/rosbridge_pingpong_test.py 를 그대로 실행해 종료 코드 0."""
    port = mock().port
    r = subprocess.run([sys.executable, "-c", REDIRECT, str(port), PINGPONG, "127.0.0.1", "3"], capture_output=True,
                       text=True, encoding="utf-8", timeout=60)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "3/3 성공" in r.stdout


def test_throttle_interval(mock, client):
    c = client(mock().port)
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry", throttle_rate=200, queue_length=1)
    got = c.collect(2.0, lambda m: m.get("topic") == "/odom")
    assert len(got) >= 7
    gaps = [b[0] - a[0] for a, b in zip(got, got[1:])]
    assert min(gaps) >= 0.18, gaps


def test_resubscribe_updates_throttle(mock, client):
    """id 없이 같은 토픽을 다시 subscribe 하면 구독이 늘지 않고 throttle 만 바뀐다."""
    c = client(mock().port)
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry")
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry", throttle_rate=500, queue_length=1)
    got = c.collect(2.0, lambda m: m.get("topic") == "/odom")
    assert 3 <= len(got) <= 6


def test_unsubscribe_stops(mock, client):
    c = client(mock().port)
    c.send(op="subscribe", topic="/imu/data_raw", type="sensor_msgs/Imu")
    assert c.until(lambda m: m.get("topic") == "/imu/data_raw")
    c.send(op="unsubscribe", topic="/imu/data_raw")
    time.sleep(0.2)
    c.collect(0.2)   # 이미 날아오던 것 버리기
    assert c.collect(1.0, lambda m: m.get("topic") == "/imu/data_raw") == []


def test_rosapi_topics_format(mock, client):
    c = client(mock("--hide-topic", "/rosout").port)
    v = c.call("/rosapi/topics")["values"]
    assert len(v["topics"]) == len(v["types"]) and v["topics"] == sorted(v["topics"])
    t = dict(zip(v["topics"], v["types"]))
    assert t["/odom"] == "nav_msgs/msg/Odometry" and t["/scan"] == "sensor_msgs/msg/LaserScan"
    assert "/rosout" not in t


def test_tf_throttle_drops_map_odom():
    """/tf 하나에 odom→base_link(33 Hz)와 map→odom(5 Hz)이 섞이면 throttle 200 · queue 1 에서 map→odom 이 대부분 사라진다.

    실제 서버를 돌리면 결과가 CPU 속도에 달린다: map 주기(200 ms)가 throttle 창과 같아 루프 지터가 없으면(빠른 리눅스)
    창이 열리는 순간에 map 이 위상 고정돼 매번 남았다. 그래서 sim.run 과 같은 순서(약 5 ms 틱마다 때가 된 발행 →
    flush)를 시드 고정 지터와 함께 Conn 에 직접 넣어 60 s 를 흉내 낸다 — 시계와 무관하게 늘 같은 결과."""
    faults = types.SimpleNamespace(frozen=lambda: False, dropped=lambda t: False, corrupt=lambda t: t, send_due=lambda: 0.0)
    server = types.SimpleNamespace(faults=faults, args=types.SimpleNamespace(cbor=False), log_tx=lambda c, p: None)
    conn = Conn(server, None, 1)
    conn.subs["/tf"] = sub = Sub("tf2_msgs/TFMessage")
    sub.update(None, 200, 1, "none")
    rng = random.Random(1)
    period = {"odom": 1 / 33, "map": 0.2}
    nxt = {"odom": 0.0, "map": 0.013}      # sim.run 처럼 map→odom 은 13 ms 어긋나게
    t = 0.0
    while t < 60.0:
        for frame in ("odom", "map"):
            while nxt[frame] <= t:
                nxt[frame] += period[frame]
                conn.offer(Packet("/tf", {"transforms": [{"header": {"frame_id": frame}}]}), t)
        conn.flush(t)
        t += 0.005 + rng.uniform(0, 0.001)
    kinds = []
    while not conn.out.empty():
        kinds.append(json.loads(conn.out.get_nowait()[1])["msg"]["transforms"][0]["header"]["frame_id"])
    assert len(kinds) >= 280                 # throttle 200 ms → 약 5 Hz
    assert kinds.count("odom") >= 220
    assert kinds.count("map") <= 60          # 5 Hz × 60 s = 300 개가 와야 하지만 대부분 덮인다(시드 1: 약 30)


def test_unknown_type_and_mismatch(mock, client):
    """Foxy rosbridge 처럼 모르는 형식 · 형식 불일치 구독에는 status 도 데이터도 없다(실기 확인)."""
    c = client(mock().port)
    c.send(op="subscribe", topic="/odom", type="foo_msgs/Bar")
    c.send(op="subscribe", topic="/imu/data_raw", type="nav_msgs/Odometry")
    c.send(op="subscribe", topic="/wheel_states", type="sensor_msgs/JointState")
    got = c.collect(1.0)
    assert {m.get("topic") for _, m in got} == {"/wheel_states"}
    assert not [m for _, m in got if m.get("op") == "status"]


def test_unknown_service(mock, client):
    c = client(mock().port)
    m = c.call("/no/such_service")
    assert m["result"] is False and m["values"] == "Service /no/such_service does not exist"


def test_no_respond(mock, client):
    c = client(mock("--no-respond", "/rosapi/topics").port)
    assert c.call("/rosapi/topics", timeout=1.0) is None
    assert c.call("/rosapi/nodes")["result"] is True


def test_inf_nan_to_null(mock, client):
    assert encode({"a": float("inf"), "b": [float("nan"), 1.5]}) == '{"a":null,"b":[null,1.5]}'
    c = client(mock("--scan-max", "1.0").port)
    c.send(op="subscribe", topic="/scan", type="sensor_msgs/LaserScan")
    c.ws.settimeout(3)
    while True:
        raw = c.ws.recv()
        if '"/scan"' in raw:
            break
    assert "null" in raw and "Infinity" not in raw and "NaN" not in raw
    scan = json.loads(raw)["msg"]
    assert len(scan["ranges"]) == 1800 and scan["header"]["frame_id"] == "base_link"
    assert (scan["angle_min"], scan["angle_increment"], scan["range_min"]) == (-3.124, 0.003483, 0.15)


def test_rx_log(mock, client):
    srv = mock()
    c = client(srv.port)
    c.send(op="advertise", topic="/teleop/cmd_vel", type="geometry_msgs/Twist")
    c.send(op="publish", topic="/teleop/cmd_vel", msg={"linear": {"x": 0.05}, "angular": {"z": 0}})
    c.call("/rosapi/nodes")
    recs = run_scenarios.read_log(srv.log)
    rx = [r for r in recs if r.get("dir") == "rx"]
    assert [r["op"] for r in rx] == ["advertise", "publish", "call_service"]
    assert rx[1]["msg"]["linear"]["x"] == 0.05 and rx[1]["wall"] > 1e9 and "t" in rx[1]


def test_cbor_silent(mock, client):
    """실기 Foxy 는 compression cbor 구독에 아무 프레임도 보내지 않는다."""
    c = client(mock().port)
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry", compression="cbor")
    c.send(op="subscribe", topic="/wheel_states", type="sensor_msgs/JointState")
    c.ws.settimeout(0.2)
    frames, deadline = [], time.time() + 1.0
    while time.time() < deadline:
        try:
            frames.append(c.ws.recv_data())
        except websocket.WebSocketTimeoutException:
            pass
    assert frames and all(op == 1 and b'"/odom"' not in data for op, data in frames)


def test_pi_suite_probe_on_mock(mock):
    """pi_suite 의 계약 점검을 모의 서버에 돌린다(실기에서는 같은 함수로 차이를 본다)."""
    import pi_suite
    p = pi_suite.probe("127.0.0.1", mock("--stamp-skew-ms", "5000").port)
    assert "/rosapi/topics" in p["services"] and "/navigate_to_pose/_action/cancel_goal" in p["services"]
    assert p["client_count"] is True and p["cbor"] is False
    assert p["scan_frame"] == "base_link" and p["scan_rays"] == 1800 and p["laser_tf"] is True
    assert 4000 < p["stamp_skew_ms"] < 6000
    assert pi_suite.diff(p, dict(p, cbor=True, services=p["services"][1:])) == {
        "cbor": {"실기": False, "모의": True}, "services": {"실기만": p["services"][:1], "모의만": []}}
