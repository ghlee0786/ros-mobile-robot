"""기록/재생 계약: --replay 순서 보존과 restamp, record_rosbridge → --replay 왕복."""
import json
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
