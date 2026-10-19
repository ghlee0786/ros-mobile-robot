"""계약 시험 공통: 모의 서버를 빈 포트로 띄우고 websocket-client 로 붙는다."""
import importlib
import json
import os
import sys
import time

import pytest
import websocket

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

run_scenarios = importlib.import_module("run_scenarios")   # 위에서 더한 경로에서 읽는다


def pytest_configure(config):
    """시험 임시 폴더를 C 드라이브가 아닌 E:/Temp 아래에 둔다(있을 때만)."""
    if not config.option.basetemp and os.path.isdir("E:/Temp"):
        os.makedirs("E:/Temp/pytest-mockbridge", exist_ok=True)
        config.option.basetemp = os.path.join("E:/Temp", "pytest-mockbridge", str(os.getpid()))


class Client:
    def __init__(self, port):
        self.ws = websocket.create_connection(f"ws://127.0.0.1:{port}", timeout=5)

    def send(self, **o):
        self.ws.send(json.dumps(o))

    def recv(self, timeout):
        self.ws.settimeout(max(0.01, timeout))
        try:
            raw = self.ws.recv()
        except websocket.WebSocketTimeoutException:
            return None
        return json.loads(raw)

    def until(self, match, timeout=3.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            m = self.recv(deadline - time.time())
            if m is not None and match(m):
                return m
        return None

    def collect(self, secs, match=lambda m: True):
        """secs 동안 match 에 맞는 (도착 시각, 메시지)."""
        out, deadline = [], time.time() + secs
        while time.time() < deadline:
            m = self.recv(deadline - time.time())
            if m is not None and match(m):
                out.append((time.perf_counter(), m))
        return out

    def call(self, service, args=None, timeout=3.0):
        sid = f"t_{service}_{time.time()}"
        self.send(op="call_service", service=service, args=args or {}, id=sid)
        return self.until(lambda m: m.get("id") == sid, timeout)

    def close(self):
        self.ws.close()


@pytest.fixture
def mock(tmp_path):
    """mock(*args) → 서버(포트 · 로그 경로). 시험이 끝나면 모두 멈춘다."""
    procs = []

    def start(*args, port=0):
        d = tmp_path / f"srv{len(procs)}"
        d.mkdir()
        srv = run_scenarios.ServerProc(str(d), list(args), port)
        procs.append(srv)
        srv.start()
        srv.log = str(d / "server.jsonl")
        return srv

    yield start
    for p in procs:
        p.stop()


@pytest.fixture
def client():
    made = []

    def connect(port):
        c = Client(port)
        made.append(c)
        return c

    yield connect
    for c in made:
        try:
            c.close()
        except Exception:
            pass
