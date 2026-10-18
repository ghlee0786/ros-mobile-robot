# 시험용 가짜 rosbridge (127.0.0.1:9090): 하트비트 · odom 을 보내고, 앱이 보낸 publish / call_service 를 mock.log 에 남긴다.
import asyncio, json, sys, os
import websockets

LOG = os.path.join(os.path.dirname(__file__), "mock.log")


async def handler(ws):
    async def feed():
        n = 0
        while True:
            await ws.send(json.dumps({"op": "publish", "topic": "/odom", "msg": {
                "header": {"stamp": {"sec": 0, "nanosec": 0}, "frame_id": "odom"},
                "pose": {"pose": {"position": {"x": 0, "y": 0, "z": 0}, "orientation": {"x": 0, "y": 0, "z": 0, "w": 1}}},
                "twist": {"twist": {"linear": {"x": 0}, "angular": {"z": 0}}}}}))
            if n % 10 == 0:
                await ws.send(json.dumps({"op": "publish", "topic": "/link/heartbeat", "msg": {"data": "ok"}}))
            n += 1
            await asyncio.sleep(0.1)
    task = asyncio.create_task(feed())
    try:
        async for raw in ws:
            m = json.loads(raw)
            if m.get("op") == "call_service":
                with open(LOG, "a", encoding="utf-8") as f:
                    f.write(json.dumps(m, ensure_ascii=False) + "\n")
                values = {"topics": ["/odom", "/link/heartbeat"], "types": ["nav_msgs/msg/Odometry", "std_msgs/msg/String"]} \
                    if m["service"] == "/rosapi/topics" else {}
                await ws.send(json.dumps({"op": "service_response", "id": m.get("id"), "service": m["service"],
                                          "result": True, "values": values}))
            elif m.get("op") == "publish" and m.get("topic") != "/teleop/cmd_vel":
                with open(LOG, "a", encoding="utf-8") as f:
                    f.write(json.dumps(m, ensure_ascii=False) + "\n")
    finally:
        task.cancel()


async def main():
    async with websockets.serve(handler, "127.0.0.1", 9090):
        await asyncio.sleep(float(sys.argv[1]) if len(sys.argv) > 1 else 120)

asyncio.run(main())
