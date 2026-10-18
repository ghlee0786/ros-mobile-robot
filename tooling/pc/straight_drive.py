"""Send a straight-drive goal to the robot over rosbridge and follow its status until it finishes.

    uv run --no-project --with websocket-client python tooling/pc/straight_drive.py 1.0 [speed] [host]
distance in metres (negative = reverse), speed in m/s (firmware max 0.1). Ctrl+C cancels and stops.
"""
import json
import sys
import time

import websocket

DIST = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
SPEED = float(sys.argv[2]) if len(sys.argv) > 2 else 0.08
HOST = sys.argv[3] if len(sys.argv) > 3 else "192.168.1.201"


def main():
    ws = websocket.create_connection(f"ws://{HOST}:9090", timeout=5)
    ws.send(json.dumps({"op": "advertise", "topic": "/straight_drive/goal", "type": "geometry_msgs/Vector3"}))
    ws.send(json.dumps({"op": "advertise", "topic": "/straight_drive/cancel", "type": "std_msgs/Empty"}))
    ws.send(json.dumps({"op": "subscribe", "topic": "/straight_drive/status", "type": "std_msgs/String"}))
    time.sleep(1.0)
    ws.send(json.dumps({"op": "publish", "topic": "/straight_drive/goal", "msg": {"x": DIST, "y": SPEED, "z": 0.0}}))
    print(f"[목표] {DIST} m @ {SPEED} m/s → ws://{HOST}:9090")
    deadline = time.time() + abs(DIST) / max(SPEED, 0.01) * 2 + 15
    try:
        while time.time() < deadline:
            ws.settimeout(max(0.1, deadline - time.time()))
            msg = json.loads(ws.recv())
            if msg.get("topic") != "/straight_drive/status":
                continue
            st = json.loads(msg["msg"]["data"])
            if st["state"] == "running":
                print(f"\r  진행 {st['along']:.3f} m | 옆 오차 {st['cross'] * 100:+.1f} cm | "
                      f"방향 오차 {st['heading_err_deg']:+.2f}° | v={st['v']:.3f} w={st['w']:+.3f}", end="")
            else:
                print(f"\n[{st['state']}] {json.dumps(st, ensure_ascii=False)}")
                if st["state"] in ("finished", "rejected"):
                    return 0
    except KeyboardInterrupt:
        print("\n[취소] 정지 명령 전송")
    ws.send(json.dumps({"op": "publish", "topic": "/straight_drive/cancel", "msg": {}}))
    time.sleep(0.5)
    return 1


if __name__ == "__main__":
    sys.exit(main())
