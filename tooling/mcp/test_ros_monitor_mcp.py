"""ROS 모니터 MCP 서버 시험: 모의 rosbridge(--nav)를 띄우고 mcp SDK 의 stdio 클라이언트로 서버를 띄워 도구를 부른다.

    checks\\mcp.cmd
    (또는) uv run --no-project --with "mcp>=1.2" --with "websockets>=12,<14" --with pytest python -m pytest tooling/mcp -q
"""
import asyncio
import json
import math
import os
import subprocess
import sys

import pytest
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SERVER = os.path.join(HERE, "ros_monitor_mcp.py")
MOCK = os.path.join(ROOT, "tooling", "mockbridge", "mock_rosbridge.py")
REGISTER = os.path.join(HERE, "register.py")


@pytest.fixture(scope="module")
def mock_url():
    proc = subprocess.Popen([sys.executable, MOCK, "--port", "0", "--nav"], stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True, cwd=os.path.dirname(MOCK))
    try:
        line = proc.stdout.readline()
        assert line.startswith("MOCKBRIDGE_PORT="), line
        yield f"ws://127.0.0.1:{int(line.split('=')[1])}"
    finally:
        proc.kill()
        proc.wait()


def run(url, script, *server_args):
    """서버를 띄워 script(call) 를 돌린다. call(name, **kw) 는 도구 결과 dict 를 돌려준다."""
    async def main():
        env = {k: v for k, v in os.environ.items() if k != "ROS_MONITOR_ALLOW_MOTION"}
        params = StdioServerParameters(command=sys.executable, args=[SERVER, "--url", url, *server_args], env=env)
        async with stdio_client(params) as (r, w):
            async with ClientSession(r, w) as s:
                await s.initialize()

                async def call(name, **kw):
                    res = await s.call_tool(name, kw)
                    # mcp 1.x 는 isError, 2.x 는 is_error
                    assert not getattr(res, "is_error", getattr(res, "isError", False)), res.content
                    return json.loads(res.content[0].text)
                call.session = s
                return await script(call)
    return asyncio.run(asyncio.wait_for(main(), 120))


def test_tools_and_reads(mock_url):
    async def script(call):
        names = {t.name for t in (await call.session.list_tools()).tools}
        assert {"robot_status", "get_pose", "get_scan_summary", "get_map_summary", "nav_status", "list_topics",
                "send_nav_goal", "cancel_nav", "estop", "drive", "straight", "set_ramp", "reset_map",
                "set_initial_pose"} <= names
        st = await call("robot_status")
        assert st["connected"] and st["allow_motion"] is False
        assert st["watchdog"]["present"] is True
        assert st["rates_hz"]["/odom"] > 5
        assert (st["nav"]["state"] == "idle") if isinstance(st["nav"], dict) else "없음" in st["nav"]   # 모의 /nav/status 가 아직 안 왔을 수 있다
        pose = await call("get_pose")
        assert pose["odom"] is not None and pose["map"] is not None
        scan = await call("get_scan_summary")
        assert scan["source"] == "/scan_compact" and len(scan["nearest_m"]) == 8
        assert all(v is None or 0 < v < 30 for v in scan["nearest_m"].values())
        mp = await call("get_map_summary", radius_m=1.0)
        assert mp["width"] > 0 and 0 < mp["known_ratio"] <= 1 and "R" in mp["preview"]
        topics = await call("list_topics")
        assert "/odom" in topics["topics"]
        nav = await call("nav_status")
        assert nav["available"] is True and nav["state"] in ("idle", "succeeded", "failed", "canceled"), nav   # 모의 서버 --nav 가 /nav/status 를 낸다
    run(mock_url, script)


def test_motion_refused_without_flag(mock_url):
    async def script(call):
        before = (await call("get_pose"))["odom"]
        for name, kw in [("drive", {"linear": 0.1, "duration_s": 1}), ("send_nav_goal", {"x": 1, "y": 0}),
                         ("straight", {"distance_m": 0.5}), ("reset_map", {}), ("set_ramp", {"enabled": False}),
                         ("set_initial_pose", {"x": 0, "y": 0})]:
            r = await call(name, **kw)
            assert r["ok"] is False and "--allow-motion" in r["refused"], (name, r)
        await asyncio.sleep(0.5)
        after = (await call("get_pose"))["odom"]
        assert math.hypot(after["x"] - before["x"], after["y"] - before["y"]) < 0.005
    run(mock_url, script)


def test_read_only_refuses_everything(mock_url):
    async def script(call):
        assert "--no-publish" in (await call("drive", linear=0.1))["refused"]
        assert (await call("estop"))["ok"] is False
        assert (await call("get_pose"))["odom"] is not None
    run(mock_url, script, "--allow-motion", "--no-publish")


def test_drive_nav_estop(mock_url):
    async def script(call):
        before = (await call("get_pose"))["odom"]
        r = await call("drive", linear=0.1, angular=0.0, duration_s=1.0)
        assert r["ok"] and 9 <= r["commands"] <= 11, r
        assert r["moved"]["distance_m"] > 0.05, r
        # 끝나면 0 을 보냈으니 멈춰 있다
        await asyncio.sleep(0.5)
        p1 = (await call("get_pose"))["odom"]
        await asyncio.sleep(0.5)
        p2 = (await call("get_pose"))["odom"]
        assert math.hypot(p2["x"] - p1["x"], p2["y"] - p1["y"]) < 0.005
        assert math.hypot(p2["x"] - before["x"], p2["y"] - before["y"]) > 0.05

        clamped = await call("drive", linear=5.0, angular=-9.0, duration_s=0.3)
        assert clamped["linear"] == 0.25 and clamped["angular"] == -0.6 and clamped["clamped"]

        m = (await call("get_pose"))["map"]
        g = await call("send_nav_goal", x=m["x"] + 0.3 * math.cos(math.radians(m["yaw_deg"])),
                       y=m["y"] + 0.3 * math.sin(math.radians(m["yaw_deg"])), yaw_deg=m["yaw_deg"], timeout_s=40)
        assert g["ok"] and g["state"] == "succeeded" and g["error_code"] == 0, g   # /nav/status 의 끝 상태
        assert math.hypot(g["final_pose"]["x"] - g["goal"]["x"], g["final_pose"]["y"] - g["goal"]["y"]) < 0.15

        # 앞쪽 2 m 목표를 보내고(기다리지 않음) 달리는 중에 비상 정지
        m = (await call("get_pose"))["map"]
        th = math.radians(m["yaw_deg"])
        await call("send_nav_goal", x=m["x"] + 2.0 * math.cos(th), y=m["y"] + 2.0 * math.sin(th), yaw_deg=m["yaw_deg"],
                   wait=False)
        await asyncio.sleep(1.5)
        assert (await call("get_pose"))["speed"]["linear_mps"] > 0.02
        e = await call("estop")
        assert e["ok"], e
        await asyncio.sleep(0.5)
        p1 = (await call("get_pose"))["odom"]
        await asyncio.sleep(1.0)
        p2 = (await call("get_pose"))["odom"]
        assert math.hypot(p2["x"] - p1["x"], p2["y"] - p1["y"]) < 0.005

        s = await call("straight", distance_m=0.2, timeout_s=20)
        assert s["ok"] and s["reason"] == "reached", s
        assert (await call("straight", distance_m=3.0))["ok"] is False
    run(mock_url, script, "--allow-motion")


def test_register_dry_run(tmp_path):
    # 실제 사용자 설정을 건드리지 않도록 HOME · APPDATA 를 임시 폴더로, Claude Code 는 --dry-run 에서만 본다
    env = {k: v for k, v in os.environ.items() if k != "CODEX_HOME"}
    env.update(HOME=str(tmp_path), USERPROFILE=str(tmp_path), APPDATA=str(tmp_path / "AppData"), PYTHONIOENCODING="utf-8")
    (tmp_path / "AppData" / "Claude").mkdir(parents=True)
    desktop = tmp_path / "AppData" / "Claude" / "claude_desktop_config.json"
    desktop.write_text(json.dumps({"mcpServers": {"other": {"command": "x"}}, "keep": 1}), encoding="utf-8")
    codex = tmp_path / ".codex" / "config.toml"
    codex.parent.mkdir()
    codex.write_text('model = "o3"\n\n[mcp_servers.other]\ncommand = "y"\n', encoding="utf-8")

    def reg(*a, claude_code=False):
        return subprocess.run([sys.executable, REGISTER, *([] if claude_code else ["--no-claude-code"]), *a], env=env,
                              capture_output=True, text=True, encoding="utf-8")

    r = reg("--dry-run", claude_code=True)
    assert r.returncode == 0, r.stderr
    assert "mcp add ros-monitor --scope user" in r.stdout and "UV_CACHE_DIR=" in r.stdout, r.stdout
    assert not codex.with_name("config.toml.bak").exists() and not list(codex.parent.glob("*.bak-*"))
    assert json.loads(desktop.read_text(encoding="utf-8"))["mcpServers"].keys() == {"other"}   # 바꾸지 않았다

    r = reg("--allow-motion")
    assert r.returncode == 0, r.stderr
    d = json.loads(desktop.read_text(encoding="utf-8"))
    assert d["keep"] == 1 and "other" in d["mcpServers"]
    assert "--allow-motion" in d["mcpServers"]["ros-monitor"]["args"]
    assert list(desktop.parent.glob("claude_desktop_config.json.bak-*"))
    t = codex.read_text(encoding="utf-8")
    assert 'model = "o3"' in t and "[mcp_servers.other]" in t and t.count("[mcp_servers.ros-monitor]") == 1
    assert "--allow-motion" in t

    r = reg()   # 다시 등록하면 갱신(중복 없음) · --allow-motion 빠짐
    assert r.returncode == 0, r.stderr
    t = codex.read_text(encoding="utf-8")
    assert t.count("[mcp_servers.ros-monitor]") == 1 and "--allow-motion" not in t and "[mcp_servers.other]" in t
    if sys.version_info >= (3, 11):
        import tomllib
        cfg = tomllib.loads(t)
        assert cfg["mcp_servers"]["ros-monitor"]["command"] and cfg["mcp_servers"]["other"]["command"] == "y"
    assert "--allow-motion" not in json.loads(desktop.read_text(encoding="utf-8"))["mcpServers"]["ros-monitor"]["args"]
