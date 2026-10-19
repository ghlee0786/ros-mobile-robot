"""장애 주입 계약: freeze · halfopen · disconnect · drop · bad-json · flood · 시간표."""
import json
import time

import websocket


def test_freeze_silences(mock, client):
    srv = mock("--freeze", "1.5@1.5")
    c = client(srv.port)
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry")
    got = c.collect(4.0, lambda m: m.get("topic") == "/odom")
    times = [t for t, _ in got]
    gaps = [b - a for a, b in zip(times, times[1:])]
    assert max(gaps) >= 1.2           # 멈춘 동안 아무것도 오지 않는다
    assert times[-1] - times[gaps.index(max(gaps)) + 1] > 0.3   # 그 뒤 다시 온다


def test_halfopen_no_pong(mock):
    srv = mock("--halfopen", "1.5")
    ws = websocket.create_connection(f"ws://127.0.0.1:{srv.port}", timeout=3)
    ws.ping("a")
    op, data = ws.recv_data(control_frame=True)
    assert op == websocket.ABNF.OPCODE_PONG and data == b"a"
    time.sleep(2.0)
    ws.send(json.dumps({"op": "subscribe", "topic": "/odom", "type": "nav_msgs/Odometry"}))
    ws.ping("b")
    ws.settimeout(2.0)
    try:
        op, data = ws.recv_data(control_frame=True)
        assert False, f"반쯤 열린 연결에서 프레임이 왔다: {op}"
    except websocket.WebSocketTimeoutException:
        pass
    ws.close(timeout=0.1)
    # 새 연결은 정상
    ws2 = websocket.create_connection(f"ws://127.0.0.1:{srv.port}", timeout=3)
    ws2.ping("c")
    assert ws2.recv_data(control_frame=True)[0] == websocket.ABNF.OPCODE_PONG
    ws2.close()


def test_fault_clock_starts_at_first_connect(mock):
    """대시보드가 서버보다 늦게 붙어도(QML 로드 2~10 s) --halfopen T 는 그 연결의 T 초 뒤에 걸린다."""
    srv = mock("--halfopen", "1.0")
    time.sleep(1.5)
    ws = websocket.create_connection(f"ws://127.0.0.1:{srv.port}", timeout=3)
    ws.ping("a")
    assert ws.recv_data(control_frame=True)[0] == websocket.ABNF.OPCODE_PONG
    time.sleep(1.5)
    ws.ping("b")
    ws.settimeout(1.5)
    try:
        op, _ = ws.recv_data(control_frame=True)
        assert False, f"연결 1 s 뒤 반쯤 열리지 않았다: {op}"
    except websocket.WebSocketTimeoutException:
        pass
    ws.close(timeout=0.1)


def test_disconnect_every(mock, client):
    c = client(mock("--disconnect-every", "1").port)
    c.ws.settimeout(3)
    t = time.time()
    try:
        while time.time() - t < 3:
            c.ws.recv()
        assert False, "끊기지 않았다"
    except (websocket.WebSocketConnectionClosedException, ConnectionError, OSError):
        pass
    assert time.time() - t < 2.5


def test_drop_topic(mock, client):
    c = client(mock("--drop", "/odom:1.0").port)
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry")
    c.send(op="subscribe", topic="/wheel_states", type="sensor_msgs/JointState")
    got = c.collect(1.0)
    topics = {m.get("topic") for _, m in got}
    assert "/wheel_states" in topics and "/odom" not in topics


def test_bad_json(mock, client):
    c = client(mock("--bad-json", "1.0").port)
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry")
    c.ws.settimeout(2)
    bad = 0
    for _ in range(10):
        raw = c.ws.recv()
        try:
            m = json.loads(raw)
            bad += "msg" not in m or "topic" not in m
        except ValueError:
            bad += 1
    assert bad == 10


def test_flood_ignores_throttle(mock, client):
    c = client(mock("--flood", "/imu/data_raw:400").port)
    c.send(op="subscribe", topic="/imu/data_raw", type="sensor_msgs/Imu", throttle_rate=50, queue_length=1)
    got = c.collect(1.0, lambda m: m.get("topic") == "/imu/data_raw")
    assert len(got) > 150


def test_script_stop_topic(mock, client, tmp_path):
    script = tmp_path / "s.json"
    script.write_text(json.dumps([{"t": 0.5, "do": "stop_topic", "topic": "/odom"}]))
    c = client(mock("--script", str(script)).port)
    time.sleep(0.8)
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry")
    c.send(op="subscribe", topic="/wheel_states", type="sensor_msgs/JointState")
    topics = {m.get("topic") for _, m in c.collect(1.0)}
    assert "/wheel_states" in topics and "/odom" not in topics


def test_delay(mock, client):
    c = client(mock("--delay-ms", "300").port)
    t = time.perf_counter()
    c.call("/rosapi/nodes")
    assert time.perf_counter() - t >= 0.28
