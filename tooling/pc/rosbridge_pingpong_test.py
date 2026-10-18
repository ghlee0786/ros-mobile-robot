"""PC -> Pi link test over rosbridge (websocket JSON, ROS-version independent).

Publishes /link/ping, waits for the Pi's /link/pong echo and reports round-trip times,
then lists the robot's topics and samples a few sensor topics.

    uv run --no-project --with websocket-client python tooling/pc/rosbridge_pingpong_test.py [host] [count]
"""
import json
import statistics
import sys
import time

import websocket

HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.200"
COUNT = int(sys.argv[2]) if len(sys.argv) > 2 else 10
SAMPLE_TOPICS = ["/link/heartbeat", "/odom", "/scan", "/imu/data_raw"]


def recv_until(ws, match, timeout):
    """Return the first incoming rosbridge message accepted by match(), or None on timeout."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        ws.settimeout(max(0.05, deadline - time.time()))
        try:
            msg = json.loads(ws.recv())
        except websocket.WebSocketTimeoutException:
            return None
        if match(msg):
            return msg
    return None


def main():
    ws = websocket.create_connection(f"ws://{HOST}:9090", timeout=5)
    print(f"[연결] ws://{HOST}:9090")
    ws.send(json.dumps({"op": "advertise", "topic": "/link/ping", "type": "std_msgs/String"}))
    ws.send(json.dumps({"op": "subscribe", "topic": "/link/pong", "type": "std_msgs/String"}))
    time.sleep(1.0)  # let the publisher/subscriber match on the Pi side

    rtts = []
    for i in range(COUNT):
        token = f"pc-rosbridge-{i}-{time.time():.6f}"
        t0 = time.perf_counter()
        ws.send(json.dumps({"op": "publish", "topic": "/link/ping", "msg": {"data": token}}))
        reply = recv_until(ws, lambda m: m.get("topic") == "/link/pong" and m["msg"]["data"].startswith(token), 2.0)
        if reply:
            rtts.append((time.perf_counter() - t0) * 1000)
            print(f"  ping {i}: pong {rtts[-1]:.1f} ms  <- {reply['msg']['data'].split('|', 1)[1]}")
        else:
            print(f"  ping {i}: 응답 없음")
        time.sleep(0.2)
    if rtts:
        print(f"[핑퐁] {len(rtts)}/{COUNT} 성공, 평균 {statistics.mean(rtts):.1f} ms, 최대 {max(rtts):.1f} ms")
    else:
        print(f"[핑퐁] 0/{COUNT} 성공")

    ws.send(json.dumps({"op": "call_service", "service": "/rosapi/topics", "id": "topics"}))
    topics = recv_until(ws, lambda m: m.get("id") == "topics", 5.0)
    if topics and topics.get("values"):
        names = topics["values"]["topics"]
        print(f"[토픽] {len(names)}개: {', '.join(sorted(names))}")

    for topic in SAMPLE_TOPICS:
        ws.send(json.dumps({"op": "subscribe", "topic": topic, "throttle_rate": 200}))
        first = recv_until(ws, lambda m, t=topic: m.get("topic") == t, 3.0)
        if first:
            text = json.dumps(first["msg"], ensure_ascii=False)
            print(f"[수신] {topic}: {text[:160]}{'...' if len(text) > 160 else ''}")
        else:
            print(f"[수신] {topic}: 3초 동안 없음")
        ws.send(json.dumps({"op": "unsubscribe", "topic": topic}))
    ws.close()
    return 0 if rtts else 1


if __name__ == "__main__":
    sys.exit(main())
