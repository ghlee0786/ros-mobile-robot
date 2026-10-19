"""rosbridge(실제 파이 또는 모의 서버)에 붙어 토픽을 대시보드와 같은 throttle 로 구독하고 JSONL 로 저장한다.

형식(대시보드 raw.jsonl 과 같은 약속):
    첫 줄  {"meta":{"host","started","topics":{"topics":[],"types":[]}}}   (topics 는 /rosapi/topics 결과)
    이후   {"t":기록 시작 기준 초,"wall":epoch 초,"topic","type","msg"}
mock_rosbridge.py --replay <파일> 로 다시 내보낼 수 있다. 로봇을 움직이는 명령은 보내지 않는다.

    uv run --no-project --with websocket-client python tooling/mockbridge/record_rosbridge.py \\
        --host 192.168.1.201 --topics /odom /scan /imu/data_raw:50 /wheel_states /tf:200 /map:2000 /plan \\
        /link/heartbeat --secs 120 --out E:\\Temp\\bags\\run1.jsonl
토픽 뒤 ':ms' 는 throttle_rate. 없으면 대시보드 기본값(imu 50 · tf 200 · map 2000 · 카메라 1000, 나머지 0).
"""
import argparse
import json
import os
import sys
import time

import websocket

DASH_THROTTLE = {"/imu/data_raw": 50, "/tf": 200, "/map": 2000, "/camera/image_raw": 1000}


def call(ws, service, args=None, timeout=5.0):
    ws.send(json.dumps({"op": "call_service", "service": service, "args": args or {}, "id": "rec_" + service}))
    deadline = time.time() + timeout
    while time.time() < deadline:
        ws.settimeout(max(0.05, deadline - time.time()))
        try:
            m = json.loads(ws.recv())
        except websocket.WebSocketTimeoutException:
            break
        if m.get("op") == "service_response" and m.get("id") == "rec_" + service:
            return m.get("values") if m.get("result", True) else None
    return None


def record(host, port, topics, secs, out):
    ws = websocket.create_connection(f"ws://{host}:{port}", timeout=5)
    listing = call(ws, "/rosapi/topics") or {"topics": [], "types": []}
    types = dict(zip(listing.get("topics", []), listing.get("types", [])))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    n = 0
    with open(out, "w", encoding="utf-8") as f:
        f.write(json.dumps({"meta": {"host": host, "started": time.time(), "topics": listing}}, ensure_ascii=False) + "\n")
        for spec in topics:
            topic, _, ms = spec.partition(":")
            throttle = int(ms) if ms else DASH_THROTTLE.get(topic, 0)
            sub = {"op": "subscribe", "topic": topic, "type": types.get(topic, "")}
            if throttle:
                sub.update(throttle_rate=throttle, queue_length=1)
            ws.send(json.dumps(sub))
        t0 = time.perf_counter()
        while (left := secs - (time.perf_counter() - t0)) > 0:
            ws.settimeout(min(1.0, left))
            try:
                raw = ws.recv()
            except websocket.WebSocketTimeoutException:
                continue
            if isinstance(raw, bytes):
                continue
            try:
                m = json.loads(raw)
            except ValueError:
                continue
            if m.get("op") == "status":
                print(f"[상태] {m.get('level')}: {m.get('msg')}", file=sys.stderr)
            if m.get("op") != "publish" or "msg" not in m:
                continue
            rec = {"t": round(time.perf_counter() - t0, 6), "wall": time.time(), "topic": m["topic"],
                   "type": types.get(m["topic"], ""), "msg": m["msg"]}
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
            n += 1
    ws.close()
    return n


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", required=True, help="rosbridge 주소")
    p.add_argument("--port", type=int, default=9090)
    p.add_argument("--topics", nargs="+", required=True, help="토픽[:throttle_ms] 목록")
    p.add_argument("--secs", type=float, default=120.0, help="기록 시간(s)")
    p.add_argument("--out", required=True, help="JSONL 파일")
    a = p.parse_args(argv)
    n = record(a.host, a.port, a.topics, a.secs, a.out)
    print(f"[기록] {n} 건 → {a.out}")
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())
