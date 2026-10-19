"""대시보드 명령줄 시험 모드(P2) 시험. 파이 없이 시험 안의 가짜 rosbridge 서버(port 0)로만 붙는다.

    uv run --no-project --with "websockets>=12,<14" --with pillow python dashboard/tests_hooks/run_hooks_test.py

dashboard/run_test.cmd 로 앱을 띄워 종료 코드 · state.json · PNG · 로그 · 레지스트리를 확인한다. 종료 코드 0 = 통과.
"""
import asyncio
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import winreg

import websockets
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent.parent
RUN = HERE / "run_test.cmd"
FAILS = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


def registry_host():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\RobotDashboard") as k:
            return winreg.QueryValueEx(k, "host")[0]
    except OSError:
        return None


class FakeBridge:
    """연결만 받고 call_service 에는 빈 응답을 돌려주는 rosbridge 흉내."""

    def __init__(self):
        self.connections = 0
        self.ops = []
        ready = threading.Event()
        self.loop = asyncio.new_event_loop()

        async def handler(ws, *_):
            self.connections += 1
            async for text in ws:
                m = json.loads(text)
                self.ops.append(m.get("op"))
                if m.get("op") == "call_service":
                    await ws.send(json.dumps({"op": "service_response", "id": m.get("id"), "service": m.get("service"),
                                              "values": {"topics": [], "types": []}, "result": True}))

        async def serve():
            self.server = await websockets.serve(handler, "127.0.0.1", 0)
            self.port = self.server.sockets[0].getsockname()[1]
            ready.set()
            await self.server.wait_closed()

        threading.Thread(target=lambda: self.loop.run_until_complete(serve()), daemon=True).start()
        ready.wait(10)

    def close(self):
        self.loop.call_soon_threadsafe(self.server.close)


def run(tmp, name, *args, actions=None, timeout=60):
    """앱을 돌리고 (종료 코드, state dict 또는 None) 을 돌려준다."""
    extra = []
    if actions is not None:
        a = tmp / f"{name}.actions.json"
        a.write_text(json.dumps(actions, ensure_ascii=False), encoding="utf-8")
        extra = ["--actions", str(a)]
    state = tmp / f"{name}.json"
    cmd = ["cmd", "/c", str(RUN), "--state", str(state), "--log", str(tmp / f"{name}.log"), *extra, *args]
    env = dict(os.environ, ROBOT_DASHBOARD_TEST="1")
    p = subprocess.run(cmd, env=env, timeout=timeout, capture_output=True)
    st = json.loads(state.read_text(encoding="utf-8")) if state.exists() else None
    print(f"[{name}] 종료 코드 {p.returncode}")
    return p.returncode, st


def main():
    reg_before = registry_host()
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="p2hooks_", dir=r"E:\Temp" if os.path.isdir(r"E:\Temp") else None))
    print(f"임시 폴더 {tmp}")

    # (1) 연결 없이 데이터 점검 화면 · 창 크기 · 캡처 · state
    png = tmp / "a.png"
    rc, st = run(tmp, "a", "--no-autoconnect", "--page", "1", "--size", "1280x800", "--screenshot", str(png),
                 "--quit-after", "4", "--fail-on-warning")
    check(rc == 0, "(1) 종료 코드 0")
    check(png.exists() and Image.open(png).size == (1280, 800), "(1) PNG 1280x800")
    check(st is not None and st["win"]["page"] == 1, "(1) state win.page == 1")
    check(st is not None and "robot" in st and "bridge" in st and "scene" in st, "(1) state 에 robot · bridge · scene")
    check(st is not None and st["bridge"]["connected"] is False, "(1) 자동 연결하지 않음")
    check(st is not None and st["exit"] == "quit-after" and "proc" in st and "render" in st, "(1) exit · proc · render")

    # (2)(3)(4) 가짜 서버에 --url 로 붙어 actions 실행, 설정은 INI 로 격리
    srv = FakeBridge()
    ini = tmp / "cfg.ini"
    shot = tmp / "b_shot.png"
    trace = tmp / "b.trace.jsonl"
    rc, st = run(tmp, "b", "--url", f"ws://127.0.0.1:{srv.port}", "--settings", str(ini), "--quit-after", "10",
                 "--trace", str(trace), "--trace-hz", "4", "--fail-on-warning",
                 actions=[{"wait": "bridge.connected", "timeout_s": 8},
                          {"t": 1, "call": "robot.setSensor", "args": ["scan", False]},
                          {"t": 1.5, "set": "win.page", "value": 2},
                          {"t": 2, "set": "scene.cam", "value": "위에서"},
                          {"t": 2.5, "shot": str(shot)}])
    srv.close()
    check(rc == 0, "(2) 종료 코드 0")
    check(srv.connections >= 1, "(2) 가짜 서버가 연결을 받음")
    check(st is not None and st["bridge"]["connected"] is True, "(2) bridge.connected == true")
    check(st is not None and st["bridge"]["port"] == srv.port, f"(2) bridge.port == {srv.port}")
    check(st is not None and st["robot"]["sensorOn"].get("scan") is False, "(3) robot.setSensor('scan', false) 반영")
    check(st is not None and st["win"]["page"] == 2, "(3) win.page == 2")
    check(st is not None and st.get("scene", {}).get("cam") == "위에서", "(3) scene.cam == 위에서")
    check(st is not None and len(st["actions_done"]) == 5, "(3) actions_done 5개")
    check(shot.exists(), "(3) shot action 캡처")
    check(trace.exists() and len(trace.read_text(encoding="utf-8").splitlines()) >= 4, "(3) trace 줄이 쌓임")
    ini_file = tmp / "cfg" / "RobotDashboardTest" / "RobotDashboard.ini"
    check(ini_file.exists() and f"port={srv.port}" in ini_file.read_text(encoding="utf-8"), "(4) 설정이 INI 에 저장됨")
    check(registry_host() == reg_before, "(4) 레지스트리 host 그대로")

    # (3) 없는 메서드 → 종료 코드 3, 그래도 state 를 남긴다
    rc, st = run(tmp, "c", "--no-autoconnect", "--settings", str(ini), "--quit-after", "3",
                 actions=[{"t": 0.5, "call": "robot.noSuchMethod", "args": []},
                          {"t": 1, "set": "win.page", "value": 1}])
    check(rc == 3, "(3) 없는 메서드 → 종료 코드 3")
    check(st is not None and st["win"]["page"] == 1 and len(st["actions_done"]) == 2, "(3) 실패 뒤에도 끝까지 진행 · state 기록")

    # QML 함수 안의 TypeError → --fail-on-warning 이면 종료 코드 2
    rc, st = run(tmp, "d", "--no-autoconnect", "--settings", str(ini), "--quit-after", "3", "--fail-on-warning",
                 actions=[{"t": 0.5, "call": "scene.setView", "args": ["없는 시점"]}])
    check(rc == 2, "경고(TypeError) → 종료 코드 2")
    check(st is not None and any("TypeError" in w for w in st["warnings"]), "state.warnings 에 TypeError")

    check(registry_host() == reg_before, "전체 실행 뒤 레지스트리 host 그대로")
    print("통과" if not FAILS else f"실패 {len(FAILS)}개: {FAILS}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
