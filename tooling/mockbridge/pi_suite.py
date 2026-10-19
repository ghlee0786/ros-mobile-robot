"""파이를 켰을 때 한 번에 도는 실기 점검(needs_pi). 로봇을 움직이는 명령은 보내지 않고 ssh·비밀번호도 쓰지 않는다.

  1) tooling/pc/rosbridge_pingpong_test.py <host>
  2) 계약 점검(probe)을 실기와 모의 서버에 똑같이 돌려 차이를 보고한다:
     rosapi 서비스 목록, /client_count 유무, cbor 지원(compression cbor 구독에 바이너리 프레임이 오는지),
     /scan frame_id, /tf_static base_link→laser, header.stamp 와 PC 시계 차
  3) record_rosbridge 로 --secs(기본 120) 초 기록
  4) 대시보드를 --url ws://<host>:9090 --no-publish 로 --dash-secs(기본 30) 초 띄워 state.json 저장

    uv run --no-project --with "websockets>=12,<14" --with websocket-client \\
        python tooling/mockbridge/pi_suite.py --host 192.168.1.201 [--out E:\\Temp\\dashtest\\pi_<시각>]
"""
import argparse
import datetime
import json
import os
import subprocess
import sys
import time

import record_rosbridge
import run_scenarios
import websocket

HERE = os.path.dirname(os.path.abspath(__file__))

PINGPONG = os.path.normpath(os.path.join(HERE, "..", "pc", "rosbridge_pingpong_test.py"))
RECORD_TOPICS = ["/odom", "/scan", "/imu/data_raw", "/wheel_states", "/wheel_cmd", "/tf", "/tf_static", "/map",
                 "/plan", "/link/heartbeat", "/straight_drive/status"]


def _wait(ws, match, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        ws.settimeout(max(0.05, deadline - time.time()))
        try:
            op, data = ws.recv_data()
        except websocket.WebSocketTimeoutException:
            return None
        if op == websocket.ABNF.OPCODE_BINARY:
            m = {"binary": True}
        else:
            try:
                m = json.loads(data)
            except ValueError:
                continue
        if match(m):
            return m
    return None


def probe(host, port, timeout=3.0):
    """rosbridge 서버의 계약 항목을 모은다(발행하지 않는다)."""
    ws = websocket.create_connection(f"ws://{host}:{port}", timeout=5)
    out = {}

    def call(service, args=None):
        ws.send(json.dumps({"op": "call_service", "service": service, "args": args or {}, "id": "probe_" + service}))
        m = _wait(ws, lambda m: m.get("id") == "probe_" + service, timeout)
        return m.get("values") if m and m.get("result", True) else None

    out["services"] = sorted((call("/rosapi/services") or {}).get("services", []))
    topics = call("/rosapi/topics") or {}
    out["topics"] = dict(zip(topics.get("topics", []), topics.get("types", [])))
    out["client_count"] = "/client_count" in out["topics"]

    ws.send(json.dumps({"op": "subscribe", "topic": "/link/heartbeat", "type": "std_msgs/String", "compression": "cbor"}))
    m = _wait(ws, lambda m: m.get("binary") or m.get("topic") == "/link/heartbeat", timeout)
    out["cbor"] = bool(m and m.get("binary"))
    ws.send(json.dumps({"op": "unsubscribe", "topic": "/link/heartbeat"}))

    ws.send(json.dumps({"op": "subscribe", "topic": "/scan", "type": "sensor_msgs/LaserScan", "throttle_rate": 500,
                        "queue_length": 1}))
    m = _wait(ws, lambda m: m.get("topic") == "/scan", timeout)
    ws.send(json.dumps({"op": "unsubscribe", "topic": "/scan"}))
    if m:
        hdr = m["msg"]["header"]
        out["scan_frame"] = hdr.get("frame_id")
        out["scan_rays"] = len(m["msg"].get("ranges", []))
        st = hdr.get("stamp", {})
        out["stamp_skew_ms"] = round((st.get("sec", 0) + st.get("nanosec", 0) / 1e9 - time.time()) * 1000, 1)

    ws.send(json.dumps({"op": "subscribe", "topic": "/tf_static", "type": "tf2_msgs/TFMessage"}))
    pairs, deadline = set(), time.time() + timeout
    while time.time() < deadline:
        m = _wait(ws, lambda m: m.get("topic") == "/tf_static", deadline - time.time())
        if not m:
            break
        for t in m["msg"].get("transforms", []):
            pairs.add(f"{t['header']['frame_id']}->{t['child_frame_id']}")
    ws.send(json.dumps({"op": "unsubscribe", "topic": "/tf_static"}))
    out["tf_static"] = sorted(pairs)
    out["laser_tf"] = "base_link->laser" in pairs
    ws.close()
    return out


def diff(real, mock):
    rep = {}
    for k in sorted(set(real) | set(mock)):
        a, b = real.get(k), mock.get(k)
        if isinstance(a, list) and isinstance(b, list):
            only_r, only_m = sorted(set(a) - set(b)), sorted(set(b) - set(a))
            if only_r or only_m:
                rep[k] = {"실기만": only_r, "모의만": only_m}
        elif isinstance(a, dict) and isinstance(b, dict):
            d = {t: [a.get(t), b.get(t)] for t in set(a) | set(b) if a.get(t) != b.get(t)}
            if d:
                rep[k] = d
        elif a != b:
            rep[k] = {"실기": a, "모의": b}
    return rep


def mock_probe(sdir):
    os.makedirs(sdir, exist_ok=True)
    srv = run_scenarios.ServerProc(sdir, [], 0)
    port = srv.start()
    try:
        return probe("127.0.0.1", port)
    finally:
        srv.stop()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="192.168.1.201")
    p.add_argument("--port", type=int, default=9090)
    p.add_argument("--secs", type=float, default=120.0, help="기록 시간")
    p.add_argument("--dash-secs", type=int, default=30, help="대시보드 실행 시간")
    p.add_argument("--exe", default=run_scenarios.DEFAULT_EXE)
    p.add_argument("--out", help=r"결과 폴더 (기본 E:\Temp\dashtest\pi_<시각>)")
    a = p.parse_args(argv)
    out = a.out or os.path.join(r"E:\Temp\dashtest", "pi_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S"))
    os.makedirs(out, exist_ok=True)
    report = {"host": a.host}

    print("[1] 핑퐁")
    r = subprocess.run([sys.executable, PINGPONG, a.host], capture_output=True, text=True, encoding="utf-8")
    print(r.stdout)
    report["pingpong_rc"] = r.returncode

    print("[2] 계약 점검")
    real = probe(a.host, a.port)
    mock = mock_probe(os.path.join(out, "mock"))
    report["probe"], report["probe_mock"], report["probe_diff"] = real, mock, diff(real, mock)
    print(json.dumps(report["probe_diff"], ensure_ascii=False, indent=1))

    print(f"[3] {a.secs:.0f} s 기록")
    bag = os.path.join(out, "pi.jsonl")
    report["recorded"] = record_rosbridge.record(a.host, a.port, RECORD_TOPICS, a.secs, bag)

    print(f"[4] 대시보드 {a.dash_secs} s")
    if os.path.exists(a.exe):
        ddir = os.path.join(out, "dash")
        os.makedirs(ddir, exist_ok=True)
        cmd = [a.exe, "--url", f"ws://{a.host}:{a.port}", "--no-publish", "--settings", os.path.join(ddir, "settings.ini"),
               "--size", "1440x900", "--screenshot", os.path.join(ddir, "shot.png"), "--state",
               os.path.join(ddir, "state.json"), "--log", os.path.join(ddir, "dash.log"), "--quit-after",
               str(a.dash_secs), "--no-model"]
        report["dashboard_rc"] = subprocess.run(cmd, env=run_scenarios.dashboard_env(), timeout=a.dash_secs + 60).returncode
    else:
        report["dashboard_rc"] = f"실행 파일 없음 {a.exe}"

    with open(os.path.join(out, "report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=1)
    print(f"[완료] {out}")
    return 0 if report["pingpong_rc"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
