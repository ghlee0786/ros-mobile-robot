"""점검 화면(P7b) 시험. 파이 없이 시험 안의 가짜 rosbridge 서버(port 0)로만 붙는다.

    uv run --no-project --with "websockets>=12,<14" python dashboard/tests_hooks/run_inspector_test.py

가짜 서버는 rosapi(topics · nodes · node_details · services), '/straight_drive/*_parameters',
/rosout 버스트, /diagnostics(한 번만 → STALE), /insp_test 를 흉내 낸다. 앱의 state.json 의 inspector 요약 속성으로 판정한다.
종료 코드 0 = 통과. (모의 서버 P1 이 끝나면 tooling/mockbridge/scenarios/insp_*.json 으로 옮길 내용)
"""
import asyncio
import json
import os
import pathlib
import shutil
import subprocess
import sys
import threading
import time

import websockets

HERE = pathlib.Path(__file__).resolve().parent.parent
RUN = HERE / "run_test.cmd"
FAILS = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


PARAMS = {
    "max_speed": ({"type": 3, "floating_point_range": [{"from_value": 0.0, "to_value": 0.5, "step": 0.0}]},
                  {"type": 3, "double_value": 0.2}),
    "use_imu": ({"type": 1}, {"type": 1, "bool_value": True}),
    "frame_id": ({"type": 4}, {"type": 4, "string_value": "base_link"}),
}


def service_values(name, args):
    if name == "/rosapi/topics":
        t = {"/rosout": "rcl_interfaces/msg/Log", "/diagnostics": "diagnostic_msgs/msg/DiagnosticArray",
             "/odom": "nav_msgs/msg/Odometry", "/insp_test": "std_msgs/msg/Float32", "/parameter_events": "rcl_interfaces/msg/ParameterEvent"}
        return {"topics": list(t), "types": list(t.values())}
    if name == "/rosapi/nodes":
        return {"nodes": ["/bringup", "/straight_drive"]}
    if name == "/rosapi/node_details":
        return {"publishing": ["/odom"], "subscribing": ["/cmd_vel"], "services": ["/straight_drive/list_parameters"]}
    if name == "/rosapi/services":
        return {"services": ["/straight_drive/list_parameters", "/slam_toolbox/save_map"]}
    if name == "/rosapi/service_type":
        return {"type": "rcl_interfaces/srv/ListParameters"}
    if name == "/straight_drive/list_parameters":
        return {"result": {"names": list(PARAMS), "prefixes": []}}
    if name == "/straight_drive/describe_parameters":
        return {"descriptors": [dict(PARAMS[n][0], name=n) for n in args.get("names", [])]}
    if name == "/straight_drive/get_parameters":
        return {"values": [PARAMS[n][1] for n in args.get("names", [])]}
    if name == "/straight_drive/set_parameters":
        return {"results": [{"successful": True, "reason": ""} for _ in args.get("parameters", [])]}
    return None


class FakeBridge:
    def __init__(self, no_respond=()):
        self.no_respond = set(no_respond)
        self.calls = []
        self.subs = {}
        self.sub_log = []
        ready = threading.Event()
        self.loop = asyncio.new_event_loop()

        async def pump(ws):
            seq = 0
            diag_sent = False
            while True:
                await asyncio.sleep(0.1)
                seq += 1
                if "/rosout" in self.subs:
                    for i in range(20):   # 초당 200줄 버스트
                        lvl = [10, 20, 30, 40, 50][i % 5]
                        await ws.send(json.dumps({"op": "publish", "topic": "/rosout", "msg": {
                            "stamp": {"sec": int(time.time()), "nanosec": 0}, "level": lvl,
                            "name": ["bringup", "straight_drive"][i % 2], "msg": f"줄 {seq}-{i}",
                            "file": "x.cpp", "function": "f", "line": i}}))
                if "/diagnostics" in self.subs and not diag_sent:   # 한 번만 보내 5 s 뒤 STALE
                    diag_sent = True
                    await ws.send(json.dumps({"op": "publish", "topic": "/diagnostics", "msg": {
                        "header": {"stamp": {"sec": 0, "nanosec": 0}, "frame_id": ""},
                        "status": [{"level": 0, "name": "모터", "message": "OK", "hardware_id": "pico",
                                    "values": [{"key": "temp", "value": "31"}]},
                                   {"level": 2, "name": "배터리", "message": "낮음", "hardware_id": "bms",
                                    "values": []}]}}))
                if "/insp_test" in self.subs:
                    await ws.send(json.dumps({"op": "publish", "topic": "/insp_test", "msg": {
                        "data": seq * 0.01, "ranges": list(range(40))}}))

        async def handler(ws, *_):
            self.subs = {}   # 다시 붙으면 구독도 새로
            task = asyncio.ensure_future(pump(ws))
            try:
                async for text in ws:
                    m = json.loads(text)
                    op = m.get("op")
                    if op == "subscribe":
                        self.subs[m["topic"]] = m
                        self.sub_log.append((m["topic"], m))
                    elif op == "unsubscribe":
                        self.subs.pop(m["topic"], None)
                    elif op == "call_service":
                        name = m.get("service")
                        self.calls.append(name)
                        if name in self.no_respond:
                            continue
                        values = service_values(name, m.get("args") or {})
                        await ws.send(json.dumps({"op": "service_response", "id": m.get("id"), "service": name,
                                                  "values": values if values is not None else "없는 서비스",
                                                  "result": values is not None}))
            except websockets.ConnectionClosed:
                pass   # 앱이 끝나며 끊음
            finally:
                task.cancel()

        async def serve():
            self.server = await websockets.serve(handler, "127.0.0.1", 0, max_size=None)
            self.port = self.server.sockets[0].getsockname()[1]
            ready.set()
            await self.server.wait_closed()

        threading.Thread(target=lambda: self.loop.run_until_complete(serve()), daemon=True).start()
        ready.wait(10)

    def close(self):
        self.loop.call_soon_threadsafe(self.server.close)


def run(tmp, name, srv, actions, quit_after, *args):
    a = tmp / f"{name}.actions.json"
    a.write_text(json.dumps(actions, ensure_ascii=False), encoding="utf-8")
    state = tmp / f"{name}.json"
    cmd = ["cmd", "/c", str(RUN), "--url", f"ws://127.0.0.1:{srv.port}", "--settings", str(tmp / f"{name}.ini"),
           "--state", str(state), "--log", str(tmp / f"{name}.log"), "--actions", str(a),
           "--screenshot", str(tmp / f"{name}.png"), "--quit-after", str(quit_after), "--fail-on-warning", *args]
    p = subprocess.run(cmd, env=dict(os.environ, ROBOT_DASHBOARD_TEST="1"), timeout=quit_after + 60, capture_output=True)
    st = json.loads(state.read_text(encoding="utf-8")) if state.exists() else None
    print(f"[{name}] 종료 코드 {p.returncode}")
    if st and st.get("warnings"):
        for w in st["warnings"][:10]:
            print("    경고: " + w)
    return p.returncode, st


def main():
    tmp = HERE / "build" / "inspector_test"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)

    # (1) 로그 · 파라미터 · 원문 · 진단 탭을 차례로 연다
    srv = FakeBridge()
    rc, st = run(tmp, "flow", srv, [
        {"wait": "bridge.connected", "timeout_s": 20},
        {"t": 0.5, "set": "win.page", "value": 1},
        {"t": 0.6, "set": "inspector.tab", "value": "로그"},
        {"t": 2.9, "shot": str(tmp / "log.png")},
        {"t": 3, "set": "inspector.tab", "value": "파라미터"},
        {"t": 3.1, "set": "paramClient.node", "value": "/straight_drive"},
        {"t": 3.2, "call": "paramClient.refresh", "args": []},
        {"t": 5, "call": "paramClient.set", "args": ["max_speed", 0.3]},
        {"t": 5.9, "shot": str(tmp / "params.png")},
        {"t": 6, "set": "inspector.tab", "value": "토픽"},
        {"t": 6.1, "set": "msgTree.topic", "value": "/insp_test"},
        {"t": 7.9, "shot": str(tmp / "topics.png")},
        {"t": 8, "set": "inspector.tab", "value": "진단"},
        {"t": 8.5, "shot": str(tmp / "diag_fresh.png")},
    ], 16)
    subs_after = dict(srv.subs)
    sub_log = list(srv.sub_log)
    srv.close()
    ins = (st or {}).get("inspector", {})
    check(rc == 0, "(1) 종료 코드 0(QML 경고 없음)")
    check(ins.get("logCount", 0) >= 20, f"(1) 로그 줄이 쌓임 logCount={ins.get('logCount')}")
    check(ins.get("paramCount") == 3, f"(1) /straight_drive 파라미터 3개 paramCount={ins.get('paramCount')}")
    check("/straight_drive/set_parameters" in srv.calls, "(1) 서버가 set_parameters 를 받음")
    check(ins.get("paramLastSuccessful") is True, "(1) set 결과 successful")
    check(ins.get("diagCount") == 2 and ins.get("diagStaleCount") == 2,
          f"(1) 진단 2개가 5 s 뒤 STALE diagCount={ins.get('diagCount')} stale={ins.get('diagStaleCount')}")
    check("/rosout" not in subs_after, f"(1) 안 보이는 로그 탭은 /rosout 구독을 끔 subs={sorted(subs_after)}")
    check(any(t == "/insp_test" and m.get("throttle_rate") == 200 for t, m in sub_log),
          "(1) 원문을 열면 그 토픽을 throttle 200 으로 구독")
    check("/insp_test" not in subs_after, "(1) 원문 탭을 떠나면 구독을 끔")
    check("/diagnostics" in subs_after, "(1) 보이는 진단 탭은 구독 중")

    # (2) --no-publish 면 파라미터 쓰기를 막는다
    srv = FakeBridge()
    rc, st = run(tmp, "nopub", srv, [
        {"wait": "bridge.connected", "timeout_s": 20},
        {"t": 0.5, "set": "win.page", "value": 1},
        {"t": 0.6, "set": "inspector.tab", "value": "파라미터"},
        {"t": 0.7, "set": "paramClient.node", "value": "/straight_drive"},
        {"t": 0.8, "call": "paramClient.refresh", "args": []},
        {"t": 3, "call": "paramClient.set", "args": ["max_speed", 0.3]},
    ], 5, "--no-publish")
    srv.close()
    ins = (st or {}).get("inspector", {})
    check(rc == 0, "(2) 종료 코드 0")
    check(ins.get("paramCount") == 3, "(2) 읽기는 됨")
    check("/straight_drive/set_parameters" not in srv.calls, "(2) --no-publish 면 set_parameters 를 보내지 않음")

    # (3) /rosapi/nodes 가 응답하지 않으면 5 s 뒤 실패 문구 · 경보
    srv = FakeBridge(no_respond={"/rosapi/nodes"})
    rc, st = run(tmp, "noresp", srv, [
        {"wait": "bridge.connected", "timeout_s": 20},
        {"t": 0.5, "set": "win.page", "value": 1},
        {"t": 0.6, "set": "inspector.tab", "value": "노드"},
        {"t": 7.5, "shot": str(tmp / "noresp_nodes.png")},
    ], 8)
    srv.close()
    ins = (st or {}).get("inspector", {})
    check(rc == 0, "(3) 종료 코드 0")
    check("응답 없음" in ins.get("rosapiError", ""), f"(3) 실패 문구 rosapiError={ins.get('rosapiError')!r}")
    check(ins.get("alertCount", 0) >= 1, f"(3) 경보 alertCount={ins.get('alertCount')}")

    print("통과" if not FAILS else f"실패 {len(FAILS)}개")
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())
