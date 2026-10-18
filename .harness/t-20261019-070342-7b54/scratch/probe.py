import json, sys, time, websocket
host = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.201"
ws = websocket.create_connection(f"ws://{host}:9090", timeout=5)
ws.send(json.dumps({"op": "call_service", "service": "/rosapi/topics", "id": "t"}))
while True:
    m = json.loads(ws.recv())
    if m.get("id") == "t":
        v = m["values"]
        for t, ty in zip(v["topics"], v["types"]):
            print(t, ty)
        break
for t in ["/wheel_states", "/wheel_cmd", "/odom", "/imu/data_raw", "/scan", "/tf", "/link/heartbeat"]:
    ws.send(json.dumps({"op": "subscribe", "topic": t, "throttle_rate": 500}))
end = time.time() + 5
seen = {}
while time.time() < end:
    ws.settimeout(1)
    try:
        m = json.loads(ws.recv())
    except Exception:
        continue
    t = m.get("topic")
    if t and t not in seen:
        seen[t] = 1
        print("==", t, json.dumps(m["msg"])[:600])
