"""P6 확인용(작업 기록): 가짜 rosbridge(port 0)가 바퀴 · IMU · odom 을 20 Hz 로 내고, 앱을 시험 모드로 띄워
armTeleop → drive → 그래프 멈춤 → 스크린샷 → state 를 본다. 종료 코드 0 = 통과.
    uv run --no-project --with "websockets>=12,<14" python .harness/<id>/dash_check.py [--locked]
"""
import asyncio, json, math, pathlib, subprocess, sys, threading, time
import websockets

HERE = pathlib.Path(__file__).resolve().parent
DASH = HERE.parent.parent / "dashboard"
LOCKED = "--locked" in sys.argv
JOY = "--joy" in sys.argv
TOPICS = {"/wheel_states": "sensor_msgs/JointState", "/wheel_cmd": "sensor_msgs/JointState",
          "/imu/data_raw": "sensor_msgs/Imu", "/odom": "nav_msgs/Odometry"}
cmd_vel = []
stamps = []
T0 = time.time()


def msg(topic, t):
    if topic in ("/wheel_states", "/wheel_cmd"):
        a = 1.5 * math.sin(t) if topic == "/wheel_states" else (1.5 if math.sin(t) > 0 else -1.5)
        return {"header": {"stamp": {"sec": int(t), "nanosec": 0}}, "name": ["left_wheel_joint", "right_wheel_joint"],
                "position": [0.0, 0.0], "velocity": [a, a * 0.9], "effort": []}
    if topic == "/imu/data_raw":
        return {"header": {"stamp": {"sec": int(t), "nanosec": 0}}, "orientation": {"x": 0, "y": 0, "z": 0, "w": 1},
                "angular_velocity": {"x": 0, "y": 0, "z": 0.3 * math.cos(2 * t)},
                "linear_acceleration": {"x": 0.2 * math.sin(3 * t), "y": 0, "z": 9.8}}
    return {"header": {"stamp": {"sec": int(t), "nanosec": 0}, "frame_id": "odom"}, "child_frame_id": "base_link",
            "pose": {"pose": {"position": {"x": 0.1 * t, "y": 0, "z": 0}, "orientation": {"x": 0, "y": 0, "z": 0, "w": 1}}},
            "twist": {"twist": {"linear": {"x": 0.05 * math.sin(t), "y": 0, "z": 0}, "angular": {"x": 0, "y": 0, "z": 0.1}}}}


async def handler(ws, *_):
    subs = set()

    async def pump():
        t0 = time.time()
        while True:
            t = time.time() - t0
            for s in list(subs):
                await ws.send(json.dumps({"op": "publish", "topic": s, "msg": msg(s, t)}))
            await asyncio.sleep(0.05)

    task = asyncio.ensure_future(pump())
    try:
        async for text in ws:
            m = json.loads(text)
            op = m.get("op")
            if op == "subscribe" and m.get("topic") in TOPICS:
                subs.add(m["topic"])
            elif op == "publish" and m.get("topic") == "/teleop/cmd_vel":
                cmd_vel.append(m["msg"]["linear"]["x"])
                stamps.append((round(time.time() - T0, 2), m["msg"]["linear"]["x"]))
            elif op == "call_service":
                vals = {"topics": list(TOPICS), "types": list(TOPICS.values())} if m.get("service") == "/rosapi/topics" else {}
                await ws.send(json.dumps({"op": "service_response", "id": m.get("id"), "service": m.get("service"),
                                          "values": vals, "result": True}))
    except websockets.ConnectionClosed:
        pass
    task.cancel()


def main():
    loop = asyncio.new_event_loop()
    ready = threading.Event()
    port = []

    async def serve():
        srv = await websockets.serve(handler, "127.0.0.1", 0)
        port.append(srv.sockets[0].getsockname()[1])
        ready.set()
        await asyncio.Future()

    threading.Thread(target=lambda: loop.run_until_complete(serve()), daemon=True).start()
    ready.wait(5)
    out = HERE / ("shot_locked" if LOCKED else "shot")
    out.mkdir(exist_ok=True)
    actions = [{"wait": "bridge.connected", "timeout_s": 10}]
    if not LOCKED:
        actions.append({"call": "robot.armTeleop", "args": [True]})
    actions += [{"call": "robot.drive", "args": [1, 0]}, {"t": 5, "call": "robot.drive", "args": [0, 0]},
                {"set": "win.page", "value": 0}, {"t": 6, "set": "wheelGraph.feeder.paused", "value": True}]
    if JOY:   # 조이스틱 값 바뀜만 drive → 10 Hz 유지, CSV · PNG 저장, 무입력 자동 잠금
        actions = [{"wait": "bridge.connected", "timeout_s": 10}, {"call": "robot.armTeleop", "args": [True]},
                   {"t": 1, "call": "joystick.send", "args": [0.5, 0]}, {"t": 4, "call": "joystick.send", "args": [0, 0]},
                   {"t": 4.5, "set": "wheelGraph.feeder.paused", "value": True}, {"t": 5, "call": "wheelGraph.saveCsv", "args": []},
                   {"call": "wheelGraph.savePng", "args": []}, {"call": "wheelGraph.fullView", "args": []},
                   {"set": "robot.armIdleMs", "value": 1000}]
    if "--scroll" in sys.argv:
        actions.append({"set": "dashScroll.contentItem.contentY", "value": int(sys.argv[sys.argv.index("--scroll") + 1])})
    (out / "actions.json").write_text(json.dumps(actions))
    args = [str(DASH / "run_test.cmd"), "--url", f"ws://127.0.0.1:{port[0]}", "--page", "0", "--size", sys.argv[sys.argv.index("--size") + 1] if "--size" in sys.argv else "1440x980",
            "--settings", str(out / "s.ini"), "--data-dir", str(out), "--no-model",
            "--actions", str(out / "actions.json"), "--quit-after", "8", "--screenshot", str(out / "shot.png"),
            "--state", str(out / "state.json"), "--log", str(out / "log.txt")]
    rc = subprocess.call(args, timeout=60)
    st = json.loads((out / "state.json").read_text(encoding="utf-8"))
    nz = [v for v in cmd_vel if v != 0]
    print("rc", rc, "armed", st["robot"]["teleopArmed"], "cmd_vel", len(cmd_vel), "max", max(cmd_vel or [0]), "nonzero", len(nz),
          "dash", {k: st.get("dash", {}).get(k) for k in ("wheelPaused", "lockHint")})
    bad = [l for l in (out / "log.txt").read_text(encoding="utf-8", errors="replace").splitlines()
           if any(w in l for w in ("TypeError", "ReferenceError", "Binding loop", "Cannot assign", "Unable to assign", "is not"))]
    for l in bad[:20]:
        print("  LOG", l)
    ok = rc == 0 and not bad and st.get("dash", {}).get("wheelPaused") is True
    if JOY:
        recs = list((out / "records").glob("wheel_*.csv")), list((out / "records").glob("wheel_*.png"))
        csv_rows = len(recs[0][-1].read_text().splitlines()) if recs[0] else 0
        print("joy nonzero", len(nz), "set", sorted(set(nz)), "csv rows", csv_rows, "png", len(recs[1]))
        ok = rc == 0 and not bad and 25 <= len(nz) <= 36 and csv_rows > 100 and recs[1] and not st["robot"]["teleopArmed"]
    elif LOCKED:
        ok = ok and not st["robot"]["teleopArmed"] and cmd_vel and not nz
    else:
        ok = ok and st["robot"]["teleopArmed"] and nz and max(cmd_vel) <= 0.10 + 1e-9
    if not ok or "--v" in sys.argv:
        print("stamps", stamps)
        print((out / "log.txt").read_text(encoding="utf-8", errors="replace")[-3000:])
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
