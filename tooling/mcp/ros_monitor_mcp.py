"""ROS 모니터 MCP 서버 — Claude · Codex(ChatGPT CLI) 같은 AI 에이전트가 로봇을 읽고 조종하게 한다.

라즈베리 파이 rosbridge(JSON v2)에 대시보드와 같은 규칙으로 붙는다(dashboard/src/robot.cpp · CONTRIBUTING.md):
속도는 /teleop/cmd_vel(파이 cmd_watchdog 경유) · 비상 정지 /teleop/estop · Nav2 목표 /goal_pose · 취소 /dashboard/nav_cancel ·
지도 초기화 /dashboard/map_reset · 초기 위치 /dashboard/initialpose · 직진 /straight_drive/goal · 가감속 /cmd_watchdog set_parameters.
구독은 throttle 을 건다(파이 rosbridge 는 한 코어가 상한).

    set UV_CACHE_DIR=E:\\Temp\\uv-cache
    uv run --no-project --with "mcp>=1.2" --with "websockets>=12,<14" python tooling/mcp/ros_monitor_mcp.py [--allow-motion]

주소: --url > ROS_MONITOR_URL > 대시보드 QSettings(HKCU\\Software\\RobotDashboard\\RobotDashboard 의 host · port) > ws://192.168.1.201:9090
움직이는 도구는 --allow-motion(또는 ROS_MONITOR_ALLOW_MOTION=1)이 있어야 하고, --no-publish 면 아무것도 보내지 않는다.
"""
import argparse
import asyncio
import itertools
import json
import math
import os
import sys
import time

import websockets

try:
    from mcp.server.fastmcp import FastMCP
except ImportError:   # mcp 2.x 는 FastMCP 를 MCPServer 로 바꿨다(생성자 · tool() · run("stdio") 쓰임새는 같다)
    from mcp.server.mcpserver import MCPServer as FastMCP

DEFAULT_HOST, DEFAULT_PORT = "192.168.1.201", 9090
MAX_V, MAX_W = 0.25, 0.6          # 펌웨어 한계(대시보드 kMaxLinear · kMaxAngular)
MAX_DRIVE_S = 5.0
MAX_STRAIGHT_M = 2.0              # 대시보드 kMaxStraight
# 토픽별 throttle 하한(ms) — 대시보드 RosBridge 표와 같다
SUBS = {
    "/odom": ("nav_msgs/Odometry", 40),
    "/tf": ("tf2_msgs/TFMessage", 200),
    "/map_odom": ("tf2_msgs/TFMessage", 200),
    "/amcl_pose": ("geometry_msgs/PoseWithCovarianceStamped", 200),
    "/straight_drive/status": ("std_msgs/String", 0),
    "/nav/status": ("std_msgs/String", 0),
    "/link/heartbeat": ("std_msgs/String", 0),
}
ADVERTISE = {
    "/teleop/cmd_vel": "geometry_msgs/Twist", "/teleop/estop": "std_msgs/Empty",
    "/goal_pose": "geometry_msgs/PoseStamped", "/dashboard/nav_cancel": "std_msgs/Empty",
    "/dashboard/map_reset": "std_msgs/Empty", "/dashboard/initialpose": "geometry_msgs/PoseWithCovarianceStamped",
    "/straight_drive/goal": "geometry_msgs/Vector3", "/straight_drive/cancel": "std_msgs/Empty",
}
NAV_DONE = ("succeeded", "failed", "canceled")
COMPASS = ["앞(0°)", "앞-왼쪽(45°)", "왼쪽(90°)", "뒤-왼쪽(135°)", "뒤(180°)", "뒤-오른쪽(-135°)", "오른쪽(-90°)", "앞-오른쪽(-45°)"]


def resolve_url(cli_url=None):
    if cli_url:
        return cli_url
    if os.environ.get("ROS_MONITOR_URL"):
        return os.environ["ROS_MONITOR_URL"]
    host, port = DEFAULT_HOST, DEFAULT_PORT
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\RobotDashboard\RobotDashboard") as k:
            for name in ("host", "port"):
                try:
                    v = winreg.QueryValueEx(k, name)[0]
                except OSError:
                    continue
                if name == "host" and str(v).strip():
                    host = str(v).strip()
                elif name == "port":
                    port = int(v)   # Qt 는 int 를 REG_SZ 또는 REG_DWORD 로 남긴다 — 둘 다 int() 로 된다
    except (ImportError, OSError, ValueError):
        pass
    return f"ws://{host}:{port}"


def yaw_of(q):
    x, y, z, w = (float(q.get(k) or 0.0) for k in "xyzw")
    if not any((x, y, z, w)):
        w = 1.0
    return math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))


def quat(yaw):
    return {"x": 0.0, "y": 0.0, "z": math.sin(yaw / 2), "w": math.cos(yaw / 2)}


def compose(a, b):
    c, s = math.cos(a[2]), math.sin(a[2])
    return (a[0] + c * b[0] - s * b[1], a[1] + s * b[0] + c * b[1], math.atan2(math.sin(a[2] + b[2]), math.cos(a[2] + b[2])))


def clamp(v, lim):
    v = float(v)
    return 0.0 if not math.isfinite(v) else max(-lim, min(lim, v))


def pose_dict(p):
    return None if p is None else {"x": round(p[0], 3), "y": round(p[1], 3), "yaw_deg": round(math.degrees(p[2]), 1)}


class Bridge:
    """rosbridge 클라이언트 하나. 처음 쓸 때 연결하고, 끊기면 다음 호출 때 다시 붙는다."""

    def __init__(self, url, publish=True):
        self.url, self.publish_ok = url, publish
        self.ws = None
        self.reader = None
        self.ids = itertools.count(1)
        self.pending = {}          # call id -> Future
        self.latest = {}           # topic -> (perf_counter, msg)
        self.times = {}            # topic -> 최근 수신 시각들(주기 계산)
        self.map_odom = None       # (x, y, yaw), 받은 시각
        self.map_odom_t = 0.0
        self.topics = None         # /rosapi/topics 결과 {이름: 형식}
        self.connected_at = None
        self.last_error = None
        self.motion = None         # 지금 이 서버가 시킨 움직임 설명
        self.stop_pending = False  # 정지를 보내지 못함 — 다시 붙으면 보낸다
        self.lock = asyncio.Lock()

    # ---- 연결 ---------------------------------------------------------------------------
    @property
    def connected(self):
        return self.ws is not None and self.reader is not None and not self.reader.done()

    async def ensure(self):
        async with self.lock:
            if self.connected:
                return
            await self._close()
            try:
                self.ws = await asyncio.wait_for(
                    websockets.connect(self.url, max_size=None, ping_interval=10, ping_timeout=10, open_timeout=5), 6)
            except Exception as e:
                self.ws, self.last_error = None, f"{self.url} 연결 실패: {e}"
                raise ConnectionError(self.last_error) from e
            self.connected_at = time.time()
            self.reader = asyncio.ensure_future(self._read())
            if self.publish_ok:
                for topic, typ in ADVERTISE.items():
                    await self._send({"op": "advertise", "topic": topic, "type": typ})
            try:
                res = await self.call("/rosapi/topics", {}, timeout=3)
                self.topics = dict(zip(res.get("topics", []), res.get("types", [])))
            except Exception:
                self.topics = None
            subs = dict(SUBS)
            # 파이 scan_compact_node 가 있으면 /scan(1800 float JSON) 대신 mm 정수 배열을 받는다
            if self.topics is None or "/scan_compact" in self.topics:
                subs["/scan_compact"] = ("std_msgs/UInt16MultiArray", 200)
            if self.topics is not None and "/scan_compact" not in self.topics:
                subs["/scan"] = ("sensor_msgs/LaserScan", 200)
            for t in self.topics or {}:
                if "battery" in t.lower():
                    subs[t] = (self.topics[t].replace("/msg/", "/"), 1000)
            for topic, (typ, thr) in subs.items():
                await self._send({"op": "subscribe", "id": f"mcp:{topic}", "topic": topic, "type": typ,
                                  "throttle_rate": thr, "queue_length": 1})
            if self.stop_pending:   # 움직이던 중 끊겨 정지를 못 보냈다: 다시 붙으면 무엇보다 먼저 멈춘다
                self.stop_pending = not await self.stop_all()

    async def _close(self):
        if self.reader:
            self.reader.cancel()
        if self.ws is not None:
            try:
                await self.ws.close()
            except Exception:
                pass
        self.ws = self.reader = None
        for f in self.pending.values():
            if not f.done():
                f.set_exception(ConnectionError("연결 끊김"))
        self.pending.clear()

    async def _read(self):
        try:
            async for raw in self.ws:
                try:
                    m = json.loads(raw)
                except (ValueError, TypeError):
                    continue
                op = m.get("op")
                if op == "publish":
                    self._on_msg(m.get("topic"), m.get("msg") or {})
                elif op == "service_response":
                    f = self.pending.pop(m.get("id"), None)
                    if f and not f.done():
                        if m.get("result") is False:
                            f.set_exception(RuntimeError(str(m.get("values"))))
                        else:
                            f.set_result(m.get("values") or {})
        except Exception as e:
            self.last_error = f"수신 끊김: {e}"
        finally:
            for f in self.pending.values():
                if not f.done():
                    f.set_exception(ConnectionError("연결 끊김"))

    def _on_msg(self, topic, msg):
        now = time.perf_counter()
        self.latest[topic] = (now, msg)
        ts = self.times.setdefault(topic, [])
        ts.append(now)
        del ts[:-30]
        if topic in ("/tf", "/map_odom"):
            for tr in msg.get("transforms") or []:
                if tr.get("header", {}).get("frame_id") == "map" and tr.get("child_frame_id") == "odom":
                    t, r = tr["transform"]["translation"], tr["transform"]["rotation"]
                    self.map_odom, self.map_odom_t = (t["x"], t["y"], yaw_of(r)), now

    async def _send(self, obj):
        if self.ws is None:
            raise ConnectionError("연결 안 됨")
        await self.ws.send(json.dumps(obj))

    async def call(self, service, args, timeout=5.0):
        cid = f"mcp_call:{next(self.ids)}"
        f = asyncio.get_running_loop().create_future()
        self.pending[cid] = f
        await self._send({"op": "call_service", "id": cid, "service": service, "args": args})
        try:
            return await asyncio.wait_for(f, timeout)
        finally:
            self.pending.pop(cid, None)

    async def publish(self, topic, msg):
        if not self.publish_ok:
            raise PermissionError("--no-publish: 읽기 전용이라 보내지 않음")
        await self._send({"op": "publish", "topic": topic, "msg": msg})

    # ---- 읽기 도우미 -------------------------------------------------------------------------
    def age(self, topic):
        m = self.latest.get(topic)
        return None if m is None else time.perf_counter() - m[0]

    def msg(self, topic, max_age=None):
        m = self.latest.get(topic)
        if m is None or (max_age is not None and time.perf_counter() - m[0] > max_age):
            return None
        return m[1]

    def rate(self, topic):
        ts = self.times.get(topic) or []
        if len(ts) < 2 or time.perf_counter() - ts[-1] > 3:
            return 0.0
        return (len(ts) - 1) / (ts[-1] - ts[0])

    def odom_pose(self):
        m = self.msg("/odom", 2.0)
        if not m:
            return None
        p = m["pose"]["pose"]
        return (p["position"]["x"], p["position"]["y"], yaw_of(p["orientation"]))

    def map_pose(self):
        """map 프레임 위치와 출처. map→odom(10 s 안) ∘ odom 이 먼저, 없으면 /amcl_pose."""
        odom = self.odom_pose()
        if odom and self.map_odom and time.perf_counter() - self.map_odom_t < 10:
            return compose(self.map_odom, odom), "map→odom ∘ /odom"
        m = self.msg("/amcl_pose", 5.0)
        if m:
            p = m["pose"]["pose"]
            return (p["position"]["x"], p["position"]["y"], yaw_of(p["orientation"])), "/amcl_pose"
        return None, None

    def nav_status(self):
        m = self.msg("/nav/status")
        if not m:
            return None
        try:
            return json.loads(m.get("data") or "{}")
        except ValueError:
            return None

    # ---- 움직임 -----------------------------------------------------------------------------
    async def twist(self, v, w):
        await self.publish("/teleop/cmd_vel", {"linear": {"x": v, "y": 0.0, "z": 0.0}, "angular": {"x": 0.0, "y": 0.0, "z": w}})

    async def stop_all(self):
        """대시보드 sendStop 과 같다: 0 Twist ×3 · 직진 취소 · Nav2 목표 취소. 실패해도 끝까지 시도한다."""
        if not self.publish_ok or self.ws is None:
            return False
        ok = True
        for topic, msg in [("/teleop/cmd_vel", None)] * 3 + [("/straight_drive/cancel", {}), ("/dashboard/nav_cancel", {})]:
            try:
                if msg is None:
                    await self.twist(0.0, 0.0)
                else:
                    await self.publish(topic, msg)
            except Exception:
                ok = False
        try:   # Nav2 가 로봇 도메인에 있는 구성용(대시보드 cancelGoal 과 같음) — 응답은 기다리지 않는다
            await self._send({"op": "call_service", "id": f"mcp_call:{next(self.ids)}",
                              "service": "/navigate_to_pose/_action/cancel_goal",
                              "args": {"goal_info": {"goal_id": {"uuid": [0] * 16}, "stamp": {"sec": 0, "nanosec": 0}}}})
        except Exception:
            ok = False
        return ok


def build(args):
    url = resolve_url(args.url)
    allow_motion = args.allow_motion or os.environ.get("ROS_MONITOR_ALLOW_MOTION") == "1"
    bridge = Bridge(url, publish=not args.no_publish)
    mcp = FastMCP("ros-monitor", instructions=(
        "ROS 모니터: 라즈베리 파이 이동 로봇(ROS 2 Foxy)을 rosbridge 로 읽고 조종한다. "
        "움직이기 전에 robot_status · get_pose · get_scan_summary 로 상태를 확인하고, 사람에게 무엇을 할지 알린 뒤 움직이고, "
        "끝나면 get_pose 로 결과를 확인한다. 위험하면 바로 estop."))

    async def ready():
        await bridge.ensure()
        # 막 붙었으면 첫 메시지들을 잠깐 기다린다(주기를 잴 수 있게 /odom 몇 개까지)
        for _ in range(20):
            if len(bridge.times.get("/odom", ())) >= 5:
                break
            await asyncio.sleep(0.1)

    def motion_refused():
        if bridge.publish_ok is False:
            return {"ok": False, "refused": "읽기 전용(--no-publish)으로 시작해 로봇에 아무것도 보내지 않는다."}
        if not allow_motion:
            return {"ok": False, "refused": "움직이는 도구가 꺼져 있다. 서버를 --allow-motion(또는 ROS_MONITOR_ALLOW_MOTION=1)으로 "
                                            "시작해야 한다 — 사람이 로봇 곁에 있을 때만 켠다."}
        return None

    async def guarded(what, body):
        """움직이는 도구 공통: 거부 판단 → 실행. 예외 · 끊김 · 취소면 정지를 보낸다."""
        refused = motion_refused()
        if refused:
            return refused
        try:
            await ready()
        except ConnectionError as e:
            return {"ok": False, "error": str(e)}
        bridge.motion = what
        try:
            return await body()
        except BaseException as e:
            stopped = await bridge.stop_all()
            bridge.stop_pending = not stopped
            if isinstance(e, asyncio.CancelledError):
                raise
            return {"ok": False, "error": f"{type(e).__name__}: {e}", "stop_sent": stopped,
                    "note": "정지를 보내지 못했으면 파이 cmd_watchdog 가 /teleop/cmd_vel 이 0.5 s 끊긴 뒤 멈춘다."}
        finally:
            bridge.motion = None

    # ---- 읽기 도구 ------------------------------------------------------------------------------
    @mcp.tool()
    async def robot_status() -> dict:
        """로봇 상태 요약: rosbridge 연결 · 주요 토픽 수신 주기(Hz) · 워치독(/cmd_watchdog) 유무와 가감속 · 직진 · Nav2 상태 ·
        배터리(토픽이 있으면) · 이 서버의 움직임 허용 여부. 움직이기 전에 먼저 부른다."""
        try:
            await ready()
        except ConnectionError as e:
            return {"connected": False, "url": url, "error": str(e)}
        rates = {t: round(bridge.rate(t), 1) for t in ("/odom", "/scan_compact", "/scan", "/tf", "/amcl_pose", "/nav/status")
                 if t in bridge.times}
        out = {"connected": bridge.connected, "url": url, "allow_motion": allow_motion, "read_only": not bridge.publish_ok,
               "rates_hz": rates, "odom_age_s": None if bridge.age("/odom") is None else round(bridge.age("/odom"), 2)}
        try:
            subs = (await bridge.call("/rosapi/subscribers", {"topic": "/teleop/cmd_vel"}, timeout=3)).get("subscribers", [])
            wd = {"present": "/cmd_watchdog" in subs, "subscribers": subs}
            if wd["present"]:
                try:
                    v = (await bridge.call("/cmd_watchdog/get_parameters", {"names": ["ramp_enabled"]}, timeout=3))["values"][0]
                    wd["ramp_enabled"] = v.get("bool_value") if v.get("type") == 1 else None
                except Exception as e:
                    wd["ramp_enabled"] = None
                    wd["ramp_error"] = str(e)
            out["watchdog"] = wd
        except Exception as e:
            out["watchdog"] = {"present": None, "error": f"rosapi 응답 없음: {e}"}
        sd = bridge.msg("/straight_drive/status")
        out["straight_drive"] = json.loads(sd["data"]) if sd and sd.get("data") else None
        out["nav"] = bridge.nav_status() or "결과 토픽 /nav/status 없음"
        out["motion_in_progress"] = bridge.motion
        bat = {t: bridge.msg(t) for t in bridge.latest if "battery" in t.lower()}
        out["battery"] = {t: {k: m.get(k) for k in ("voltage", "percentage", "current") if k in m} for t, m in bat.items()} or "배터리 토픽 없음"
        hb = bridge.msg("/link/heartbeat", 3.0)
        out["heartbeat"] = hb.get("data") if hb else None
        return out

    @mcp.tool()
    async def get_pose() -> dict:
        """로봇 위치 · 방향: odom 프레임(바퀴 엔코더)과 map 프레임(SLAM/AMCL, 있으면). 단위 m · 도. 속도도 함께."""
        try:
            await ready()
        except ConnectionError as e:
            return {"error": str(e)}
        mp, src = bridge.map_pose()
        # 막 붙었으면 map 쪽(/amcl_pose 2 Hz · map→odom 5 Hz)은 아직 안 왔을 수 있다 — 토픽이 있으면 잠깐 기다린다
        for _ in range(15):
            if mp or not {"/amcl_pose", "/map_odom"} & set(bridge.topics or ()):
                break
            await asyncio.sleep(0.1)
            mp, src = bridge.map_pose()
        odom = bridge.odom_pose()
        m = bridge.msg("/odom", 2.0)
        tw = m["twist"]["twist"] if m else None
        return {"odom": pose_dict(odom), "map": pose_dict(mp), "map_source": src or "map 위치 없음(map→odom · /amcl_pose 없음)",
                "speed": {"linear_mps": round(tw["linear"]["x"], 3), "angular_radps": round(tw["angular"]["z"], 3)} if tw else None}

    def scan_ranges():
        """(각도[rad, 로봇 기준], 거리[m]) 목록. /scan_compact(mm 정수) 또는 /scan."""
        m = bridge.msg("/scan_compact", 2.0)
        if m:
            label = m["layout"]["dim"][0]["label"].split()
            a0, inc = float(label[1]), float(label[2])
            return [(a0 + i * inc, d / 1000.0) for i, d in enumerate(m.get("data") or []) if d], "/scan_compact"
        m = bridge.msg("/scan", 2.0)
        if m:
            a0, inc = m["angle_min"], m["angle_increment"]
            return [(a0 + i * inc, r) for i, r in enumerate(m.get("ranges") or [])
                    if r is not None and math.isfinite(r) and m["range_min"] <= r <= m["range_max"]], "/scan"
        return None, None

    @mcp.tool()
    async def get_scan_summary() -> dict:
        """LiDAR 요약: 8 방위(앞=0°, 왼쪽=+90°)마다 ±22.5° 안의 최근접 거리(m). 장애물 확인용."""
        try:
            await ready()
            for _ in range(20):
                rays, src = scan_ranges()
                if rays is not None:
                    break
                await asyncio.sleep(0.1)
        except ConnectionError as e:
            return {"error": str(e)}
        if rays is None:
            return {"error": "LiDAR 데이터 없음(/scan_compact · /scan 이 2 s 안에 오지 않음)"}
        near = [None] * 8
        for a, r in rays:
            k = int(((math.degrees(a) + 22.5) % 360) // 45)
            if near[k] is None or r < near[k]:
                near[k] = r
        closest = min(rays, key=lambda x: x[1]) if rays else None
        return {"source": src, "rays": len(rays),
                "nearest_m": {COMPASS[i]: None if near[i] is None else round(near[i], 2) for i in range(8)},
                "closest": {"distance_m": round(closest[1], 2), "bearing_deg": round(math.degrees(math.atan2(
                    math.sin(closest[0]), math.cos(closest[0]))), 0)} if closest else None}

    @mcp.tool()
    async def get_map_summary(radius_m: float = 2.0) -> dict:
        """SLAM 지도(/map) 요약: 크기 · 해상도 · 알려진 칸 비율 · 로봇 주변 ASCII 미리보기
        ('#' 벽 · '.' 빈 곳 · ' ' 모름 · 'R' 로봇, 위쪽 = map +y). 지도는 크므로 한 번만 받아 온다."""
        try:
            await ready()
        except ConnectionError as e:
            return {"error": str(e)}
        sid = "mcp:/map"
        bridge.latest.pop("/map", None)
        await bridge._send({"op": "subscribe", "id": sid, "topic": "/map", "type": "nav_msgs/OccupancyGrid",
                            "throttle_rate": 2000, "queue_length": 1})
        try:
            for _ in range(80):
                if "/map" in bridge.latest:
                    break
                await asyncio.sleep(0.1)
        finally:   # 큰 지도를 계속 받지 않도록 바로 끊는다
            await bridge._send({"op": "unsubscribe", "id": sid, "topic": "/map"})
        m = bridge.msg("/map")
        if not m:
            return {"error": "지도 없음(/map 이 8 s 안에 오지 않음 — SLAM 이 돌고 있는지 확인)"}
        info, data = m["info"], m.get("data") or []
        w, h, res = info["width"], info["height"], info["resolution"]
        o = info["origin"]
        org = (o["position"]["x"], o["position"]["y"], yaw_of(o["orientation"]))
        known = sum(1 for v in data if v >= 0)
        out = {"width": w, "height": h, "resolution_m": res, "size_m": [round(w * res, 2), round(h * res, 2)],
               "origin": pose_dict(org), "known_ratio": round(known / len(data), 3) if data else 0.0}
        rp, _ = bridge.map_pose()
        center = rp or (org[0] + w * res / 2, org[1] + h * res / 2, 0.0)
        step = max(res, 2 * min(max(radius_m, 0.5), 10.0) / 40)   # 미리보기 41×41 이하
        n = int(min(max(radius_m, 0.5), 10.0) / step)
        rows = []
        c, s = math.cos(-org[2]), math.sin(-org[2])
        for j in range(n, -n - 1, -1):
            row = []
            for i in range(-n, n + 1):
                if i == 0 and j == 0 and rp:
                    row.append("R")
                    continue
                dx, dy = center[0] + i * step - org[0], center[1] + j * step - org[1]
                gx, gy = int((c * dx - s * dy) / res), int((s * dx + c * dy) / res)
                v = data[gy * w + gx] if 0 <= gx < w and 0 <= gy < h else -1
                row.append(" " if v < 0 else "#" if v >= 50 else ".")
            rows.append("".join(row))
        out.update({"preview_center": pose_dict(center), "preview_cell_m": round(step, 3), "preview": "\n".join(rows)})
        return out

    @mcp.tool()
    async def nav_status() -> dict:
        """Nav2 진행 상태(/nav/status 최신): state(idle|planning|running|succeeded|failed|canceled) · 목표 · 남은 거리 ·
        ETA · 복구 횟수 · 오류 코드 · 한국어 이유."""
        try:
            await ready()
        except ConnectionError as e:
            return {"error": str(e)}
        st = bridge.nav_status()
        if st is None:
            return {"available": False, "note": "결과 토픽 /nav/status 없음 — PC 내비 스택이 아직 이 토픽을 내지 않는다. "
                                                "send_nav_goal 은 위치 변화로 도착을 추정한다."}
        return {"available": True, "age_s": round(bridge.age("/nav/status"), 2), **st}

    @mcp.tool()
    async def list_topics() -> dict:
        """rosbridge 에 보이는 토픽과 형식 목록(/rosapi/topics)."""
        try:
            await ready()
            res = await bridge.call("/rosapi/topics", {}, timeout=5)
        except Exception as e:
            return {"error": str(e)}
        bridge.topics = dict(zip(res.get("topics", []), res.get("types", [])))
        return {"count": len(bridge.topics), "topics": bridge.topics}

    # ---- 쓰기 도구(로봇이 움직인다) ----------------------------------------------------------------
    @mcp.tool()
    async def estop() -> dict:
        """비상 정지: /teleop/estop(가감속 없이 즉시 0) + 0 속도 ×3 + 직진 취소 + Nav2 목표 취소.
        --allow-motion 이 없어도 된다(멈추는 것은 언제나 허용). --no-publish 면 보내지 않는다."""
        if not bridge.publish_ok:
            return {"ok": False, "refused": "읽기 전용(--no-publish)"}
        try:
            await bridge.ensure()
            await bridge.publish("/teleop/estop", {})
        except Exception as e:
            return {"ok": False, "error": str(e), "note": "보내지 못했다 — 파이 cmd_watchdog 가 0.5 s 뒤 멈춘다(명령이 끊기면)."}
        return {"ok": await bridge.stop_all(), "sent": ["/teleop/estop", "/teleop/cmd_vel 0 ×3", "/straight_drive/cancel",
                                                        "/dashboard/nav_cancel"]}

    @mcp.tool()
    async def drive(linear: float, angular: float = 0.0, duration_s: float = 1.0) -> dict:
        """속도 조종: /teleop/cmd_vel 을 10 Hz 로 duration_s(≤ 5 s) 동안 보내고 끝나면 0 을 보낸다.
        linear m/s(앞 +, |v| ≤ 0.25) · angular rad/s(왼쪽 +, |w| ≤ 0.6) 로 자른다. 도중에 연결이 끊기면 파이 cmd_watchdog 가
        0.5 s 뒤 멈춘다. --allow-motion 필요."""
        v, w = clamp(linear, MAX_V), clamp(angular, MAX_W)
        dur = max(0.0, min(MAX_DRIVE_S, float(duration_s) if math.isfinite(duration_s) else 0.0))

        async def body():
            start = bridge.odom_pose()
            t0 = time.perf_counter()
            k = 0
            # 워치독은 0.5 s 안에 다음 명령이 없으면 멈춘다 — 0.1 s 마다 보낸다(대시보드 m_teleop 과 같은 10 Hz)
            while time.perf_counter() - t0 < dur:
                await bridge.twist(v, w)
                k += 1
                await asyncio.sleep(max(0.0, t0 + k * 0.1 - time.perf_counter()))
            for _ in range(3):
                await bridge.twist(0.0, 0.0)
            await asyncio.sleep(0.3)
            end = bridge.odom_pose()
            moved = None
            if start and end:
                moved = {"distance_m": round(math.hypot(end[0] - start[0], end[1] - start[1]), 3),
                         "turn_deg": round(math.degrees(math.atan2(math.sin(end[2] - start[2]), math.cos(end[2] - start[2]))), 1)}
            return {"ok": True, "linear": v, "angular": w, "duration_s": round(dur, 2), "commands": k,
                    "clamped": v != linear or w != angular or dur != duration_s, "moved": moved, "odom_end": pose_dict(end)}
        return await guarded(f"drive v={v} w={w}", body)

    async def wait_nav(goal, t_sent, timeout_s):
        """끝 상태를 기다린다. /nav/status 가 있으면 그것으로, 없으면 위치로 추정한다."""
        last_progress, best = time.perf_counter(), None
        estimate_note = "결과 토픽 /nav/status 없음 — 위치 변화로 도착 여부를 추정했다"
        while time.perf_counter() - t_sent < timeout_s:
            await asyncio.sleep(0.2)
            if not bridge.connected:
                raise ConnectionError("기다리는 중 연결 끊김")
            st = bridge.nav_status()
            fresh = st is not None and bridge.latest["/nav/status"][0] > t_sent
            if fresh and st.get("state") in NAV_DONE:
                return {"ok": st["state"] == "succeeded", "state": st["state"], "reason": st.get("reason"),
                        "error_code": st.get("error_code"), "error_msg": st.get("error_msg"),
                        "recoveries": st.get("recoveries"), "source": "/nav/status"}
            if fresh:
                continue
            p, _ = bridge.map_pose()
            if p is None:
                continue
            d = math.hypot(goal[0] - p[0], goal[1] - p[1])
            m = bridge.msg("/odom", 2.0)
            still = m is None or (abs(m["twist"]["twist"]["linear"]["x"]) < 0.01 and abs(m["twist"]["twist"]["angular"]["z"]) < 0.02)
            if best is None or d < best - 0.02:
                best, last_progress = d, time.perf_counter()
            if d < 0.15 and still and time.perf_counter() - t_sent > 1.0:
                return {"ok": True, "state": "succeeded(추정)", "reason": f"목표에서 {d:.2f} m 안에 멈춤", "source": estimate_note}
            if time.perf_counter() - last_progress > 20:
                return {"ok": False, "state": "failed(추정)", "reason": f"20 s 동안 목표에 가까워지지 않음(남은 {d:.2f} m)",
                        "source": estimate_note}
        return None

    @mcp.tool()
    async def send_nav_goal(x: float, y: float, yaw_deg: float = 0.0, wait: bool = True, timeout_s: float = 120.0) -> dict:
        """Nav2 이동 목표(map 프레임, m · 도)를 /goal_pose 로 보낸다. wait=true 면 끝날 때까지(최대 timeout_s) 기다려
        끝 상태(succeeded · failed · canceled)와 한국어 이유를 돌려준다(/nav/status, 없으면 위치로 추정).
        시간이 다 되면 목표를 취소하고 멈춘다. --allow-motion 필요."""
        if not all(math.isfinite(v) for v in (x, y, yaw_deg)):
            return {"ok": False, "error": "숫자가 아닌 목표"}
        goal = (float(x), float(y), math.radians(yaw_deg))

        async def body():
            await bridge.publish("/goal_pose", {"header": {"frame_id": "map", "stamp": {"sec": 0, "nanosec": 0}},
                                                "pose": {"position": {"x": goal[0], "y": goal[1], "z": 0.0},
                                                         "orientation": quat(goal[2])}})
            t_sent = time.perf_counter()
            if not wait:
                return {"ok": True, "sent": pose_dict(goal), "note": "기다리지 않음 — nav_status · get_pose 로 확인한다"}
            res = await wait_nav(goal, t_sent, max(1.0, float(timeout_s)))
            if res is None:
                await bridge.stop_all()
                res = {"ok": False, "state": "timeout", "reason": f"{timeout_s:.0f} s 안에 끝나지 않아 목표를 취소하고 멈췄다"}
            p, _ = bridge.map_pose()
            return {**res, "goal": pose_dict(goal), "final_pose": pose_dict(p),
                    "elapsed_s": round(time.perf_counter() - t_sent, 1)}
        return await guarded(f"nav goal {pose_dict(goal)}", body)

    @mcp.tool()
    async def cancel_nav() -> dict:
        """진행 중인 Nav2 목표를 모두 취소하고 0 속도를 보낸다(/dashboard/nav_cancel). 멈추는 명령이라 --allow-motion 없이도 된다."""
        if not bridge.publish_ok:
            return {"ok": False, "refused": "읽기 전용(--no-publish)"}
        try:
            await bridge.ensure()
        except ConnectionError as e:
            return {"ok": False, "error": str(e)}
        return {"ok": await bridge.stop_all()}

    @mcp.tool()
    async def straight(distance_m: float, timeout_s: float = 60.0) -> dict:
        """직진(파이 straight_drive 노드, IMU 로 방향 유지): distance_m 만큼(|d| ≤ 2 m, 음수 = 후진) 가고 끝 상태를 돌려준다.
        시간이 다 되면 취소한다. --allow-motion 필요."""
        if not math.isfinite(distance_m) or abs(distance_m) > MAX_STRAIGHT_M or distance_m == 0:
            return {"ok": False, "error": f"거리 {distance_m} m 가 범위(0 < |d| ≤ {MAX_STRAIGHT_M} m) 밖"}

        async def body():
            t_sent = time.perf_counter()
            # y = 0 → 노드의 default_speed
            await bridge.publish("/straight_drive/goal", {"x": float(distance_m), "y": 0.0, "z": 0.0})
            while time.perf_counter() - t_sent < timeout_s:
                await asyncio.sleep(0.2)
                if not bridge.connected:
                    raise ConnectionError("직진 중 연결 끊김")
                rec = bridge.latest.get("/straight_drive/status")
                if rec and rec[0] > t_sent:
                    st = json.loads(rec[1].get("data") or "{}")
                    if st.get("state") in ("finished", "rejected"):
                        return {"ok": st.get("state") == "finished" and st.get("reason") == "reached", **st}
            await bridge.stop_all()
            return {"ok": False, "state": "timeout", "reason": f"{timeout_s:.0f} s 안에 끝나지 않아 취소했다"}
        return await guarded(f"straight {distance_m} m", body)

    @mcp.tool()
    async def set_ramp(enabled: bool) -> dict:
        """파이 cmd_watchdog 의 가감속 프로파일(ramp_enabled)을 켜거나 끈다. 끄면 명령 속도가 바로 나간다. --allow-motion 필요."""
        async def body():
            res = await bridge.call("/cmd_watchdog/set_parameters", {"parameters": [
                {"name": "ramp_enabled", "value": {"type": 1, "bool_value": bool(enabled)}}]}, timeout=5)
            r = (res.get("results") or [{}])[0]
            return {"ok": bool(r.get("successful")), "ramp_enabled": bool(enabled), "reason": r.get("reason") or None}
        return await guarded(None, body)

    @mcp.tool()
    async def reset_map() -> dict:
        """SLAM 지도를 비우고 지금 자리에서 새로 그린다(/dashboard/map_reset → PC slam_toolbox reset).
        Nav2 가 '출발점이 막힘' 으로 실패할 때 쓴다. --allow-motion 필요."""
        async def body():
            await bridge.publish("/dashboard/map_reset", {})
            return {"ok": True, "sent": "/dashboard/map_reset", "note": "몇 초 뒤 get_map_summary 로 새 지도를 확인한다"}
        return await guarded(None, body)

    @mcp.tool()
    async def set_initial_pose(x: float, y: float, yaw_deg: float = 0.0) -> dict:
        """AMCL 초기 위치(map 프레임, m · 도)를 /dashboard/initialpose 로 알린다. 위치 추정이 틀렸을 때. --allow-motion 필요."""
        if not all(math.isfinite(v) for v in (x, y, yaw_deg)):
            return {"ok": False, "error": "숫자가 아닌 위치"}

        async def body():
            cov = [0.0] * 36
            cov[0] = cov[7] = 0.25          # 대시보드 msgparse::initialPoseCovariance 와 같다(0.5 m · 15°)
            cov[35] = (math.pi / 12) ** 2
            p = (float(x), float(y), math.radians(yaw_deg))
            await bridge.publish("/dashboard/initialpose", {
                "header": {"frame_id": "map", "stamp": {"sec": 0, "nanosec": 0}},
                "pose": {"pose": {"position": {"x": p[0], "y": p[1], "z": 0.0}, "orientation": quat(p[2])}, "covariance": cov}})
            return {"ok": True, "sent": pose_dict(p)}
        return await guarded(None, body)

    return mcp


def main(argv=None):
    p = argparse.ArgumentParser(description="ROS 모니터 MCP 서버(stdio)")
    p.add_argument("--url", help="rosbridge 주소 ws://host:port (기본: ROS_MONITOR_URL > 대시보드 설정 > ws://192.168.1.201:9090)")
    p.add_argument("--allow-motion", action="store_true", help="로봇을 움직이는 도구를 켠다(ROS_MONITOR_ALLOW_MOTION=1 과 같음)")
    p.add_argument("--no-publish", action="store_true", help="읽기 전용: 로봇에 아무것도 보내지 않는다")
    args = p.parse_args(argv)
    build(args).run("stdio")


if __name__ == "__main__":
    sys.exit(main())
