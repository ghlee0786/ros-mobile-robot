"""기록/재생 계약: --replay 순서 보존과 restamp, record_rosbridge → --replay 왕복."""
import json
import math
import os
import time

import record_rosbridge
from mockbridge.replay import read_jsonl

from conftest import HERE

SAMPLE = os.path.join(HERE, "data", "sample.jsonl")


def stamp_s(m):
    s = m["msg"]["header"]["stamp"]
    return s["sec"] + s["nanosec"] / 1e9


def test_replay_order(mock, client):
    c = client(mock("--replay", SAMPLE, "--rate", "2").port)
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry")
    got = [m for _, m in c.collect(1.5, lambda m: m.get("topic") == "/odom")]
    assert len(got) >= 20
    stamps = [stamp_s(m) for m in got]
    assert stamps == sorted(stamps) and len(set(stamps)) == len(stamps)
    assert stamps[0] < 1792400003      # 원래 시각 그대로
    _, recs = read_jsonl(SAMPLE)
    want = [stamp_s(r) for r in recs if r["topic"] == "/odom"]
    assert stamps == want[want.index(stamps[0]):][:len(stamps)]


def test_replay_restamp_loop(mock, client):
    c = client(mock("--replay", SAMPLE, "--restamp", "--loop", "--rate", "4").port)
    c.send(op="subscribe", topic="/tf", type="tf2_msgs/TFMessage")
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry")
    got = c.collect(1.5)
    odom = [m for _, m in got if m.get("topic") == "/odom"]
    tf = [m for _, m in got if m.get("topic") == "/tf"]
    assert len(odom) > 60            # 2 s 기록을 4 배속 반복 → 1.5 s 에 한 바퀴 넘게
    assert all(abs(stamp_s(m) - time.time()) < 2 for m in odom)
    st = tf[-1]["msg"]["transforms"][0]["header"]["stamp"]
    assert abs(st["sec"] - time.time()) < 3


def test_replay_commands_logged(mock, client):
    srv = mock("--replay", SAMPLE)
    c = client(srv.port)
    c.send(op="advertise", topic="/teleop/cmd_vel", type="geometry_msgs/Twist")
    c.send(op="publish", topic="/teleop/cmd_vel", msg={"linear": {"x": 0.1}, "angular": {"z": 0}})
    assert c.call("/rosapi/topics")["values"]["topics"]
    with open(srv.log, encoding="utf-8") as f:
        ops = [json.loads(line).get("op") for line in f]
    assert "publish" in ops


def test_record_roundtrip(mock, client, tmp_path):
    srv = mock()
    out = str(tmp_path / "rec.jsonl")
    n = record_rosbridge.record("127.0.0.1", srv.port, ["/odom", "/imu/data_raw", "/link/heartbeat", "/tf"], 2.5, out)
    assert n > 50
    meta, recs = read_jsonl(out)
    assert meta["host"] == "127.0.0.1" and "/odom" in meta["topics"]["topics"]
    imu = [r["t"] for r in recs if r["topic"] == "/imu/data_raw"]
    assert imu and min(b - a for a, b in zip(imu, imu[1:])) >= 0.045      # 대시보드와 같은 50 ms throttle
    assert all(r["type"] for r in recs) and all(r["wall"] > 1e9 for r in recs)
    rep = mock("--replay", out)
    c = client(rep.port)
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry")
    got = [m["msg"] for _, m in c.collect(2.0, lambda m: m.get("topic") == "/odom")]
    rec_odom = [r["msg"] for r in recs if r["topic"] == "/odom"]
    assert got and got == rec_odom[rec_odom.index(got[0]):][:len(got)]


def write_rec(path, recs, topics=None):
    with open(path, "w", encoding="utf-8") as f:
        f.write(json.dumps({"meta": {"host": "t", "started": 0, "topics": topics or []}}) + "\n")
        for r in recs:
            f.write(json.dumps(r) + "\n")


def test_replay_jittered_throttled_rate(mock, client, tmp_path):
    """B32: 파이 rosbridge 의 50 ms throttle 을 거친 18.5 Hz 를 PC 가 15.6 ms 단위로 받은 기록(간격 절반이 50 ms 미만)을
    같은 50 ms throttle 구독으로 재생해도 기록 빈도의 95 % 이상이 나가야 한다(전에는 약 80 %)."""
    import random
    rnd = random.Random(3)
    q = 0.015625
    ts = sorted(math.floor((i * 0.054 + rnd.uniform(0, 0.03)) / q) * q for i in range(200))
    imu = {"header": {"stamp": {"sec": 0, "nanosec": 0}, "frame_id": "imu"}, "orientation": {"w": 1.0}}
    path = str(tmp_path / "imu.jsonl")
    write_rec(path, [{"t": t, "topic": "/imu/data_raw", "type": "sensor_msgs/msg/Imu", "msg": imu} for t in ts])
    c = client(mock("--replay", path, "--loop").port)
    c.send(op="subscribe", topic="/imu/data_raw", type="sensor_msgs/Imu", throttle_rate=50, queue_length=1)
    got = c.collect(6.0, lambda m: m.get("topic") == "/imu/data_raw")
    rec_hz = (len(ts) - 1) / (ts[-1] - ts[0])
    hz = (len(got) - 1) / (got[-1][0] - got[0][0])
    assert hz >= 0.95 * rec_hz, (hz, rec_hz)
    gaps = [b[0] - a[0] for a, b in zip(got, got[1:])]
    assert sum(gaps) / len(gaps) >= 0.05 * 0.95        # 평균은 여전히 throttle 주기보다 짧지 않다


def test_replay_latched_tf_static_all(mock, client, tmp_path):
    """B17: /tf_static 이 발행자마다 따로 기록돼 있으면 늦게 구독해도 모두(base_footprint · imu · laser) 받는다."""
    def tf(child):
        return {"transforms": [{"header": {"stamp": {"sec": 0, "nanosec": 0}, "frame_id": "base_link"},
                                "child_frame_id": child,
                                "transform": {"translation": {"x": 0, "y": 0, "z": 0}, "rotation": {"w": 1}}}]}
    recs = [{"t": 0.01 * i, "topic": "/tf_static", "type": "tf2_msgs/msg/TFMessage", "msg": tf(c)}
            for i, c in enumerate(["base_footprint", "imu", "laser"])]
    recs.append({"t": 0.05, "topic": "/tf_static", "type": "tf2_msgs/msg/TFMessage", "msg": tf("laser")})   # 같은 발행자 다시
    path = str(tmp_path / "tf.jsonl")
    write_rec(path, recs)
    srv = mock("--replay", path)
    time.sleep(1.0)                                     # 재생이 끝난 뒤에 구독한다
    c = client(srv.port)
    c.send(op="subscribe", topic="/tf_static", type="tf2_msgs/TFMessage", throttle_rate=0, queue_length=1)
    got = c.collect(1.0, lambda m: m.get("topic") == "/tf_static")
    children = [tr["child_frame_id"] for _, m in got for tr in m["msg"]["transforms"]]
    assert sorted(children) == ["base_footprint", "imu", "laser"]


def test_replay_dashboard_raw(mock, client, tmp_path):
    """B18: 대시보드 기록(raw.jsonl: meta.topics 가 이름 목록)도 --replay 로 그대로 재생된다."""
    odom = {"header": {"stamp": {"sec": 1, "nanosec": 0}, "frame_id": "odom"}, "child_frame_id": "base_link"}
    path = str(tmp_path / "raw.jsonl")
    write_rec(path, [{"t": 0.02 * i, "wall": 1792443971.0 + 0.02 * i, "topic": "/odom", "type": "nav_msgs/msg/Odometry",
                      "msg": odom} for i in range(50)], topics=["/odom"])
    c = client(mock("--replay", path, "--restamp", "--loop").port)
    assert c.call("/rosapi/topics")["values"]["topics"] == ["/odom"]
    c.send(op="subscribe", topic="/odom", type="nav_msgs/Odometry")
    assert c.until(lambda m: m.get("topic") == "/odom", 3.0)
