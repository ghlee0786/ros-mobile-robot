"""3D 장면(P8b) 시험. 파이 없이 시험 안의 가짜 rosbridge 서버(port 0)에서 /odom · /scan · /tf · /map · 코스트맵 ·
footprint · /amcl_pose 를 흘리고 dashboard/run_test.cmd 로 앱을 띄워 state · PNG 를 확인한다. 종료 코드 0 = 통과.

    uv run --no-project --with "websockets>=12,<14" --with pillow python dashboard/tests_hooks/run_scene_test.py [--with-model]

--with-model 이면 model/ 메시를 읽어 실제와 같은 렌더 부하에서 fps 를 본다(기본은 --no-model).
"""
import asyncio
import json
import math
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import time

import websockets
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent.parent
RUN = HERE / "run_test.cmd"
FAILS = []


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


def quat(yaw):
    return {"x": 0, "y": 0, "z": math.sin(yaw / 2), "w": math.cos(yaw / 2)}


def tf(parent, child, x, y, yaw):
    return {"header": {"frame_id": parent, "stamp": {"sec": 0, "nanosec": 0}}, "child_frame_id": child,
            "transform": {"translation": {"x": x, "y": y, "z": 0}, "rotation": quat(yaw)}}


def grid(frame, w, h, res, ox, oy, data):
    return {"header": {"frame_id": frame}, "info": {"width": w, "height": h, "resolution": res,
            "origin": {"position": {"x": ox, "y": oy, "z": 0}, "orientation": quat(0)}}, "data": data}


def flip_map(origin_yaw_deg=0):
    """map 프레임 4 m × 4 m 판, 로봇 앞 +1 m(x 0.9..1.1, y -0.1..0.1)와 상하 뒤집힘을 잡는 왼쪽 +1 m 표식
    (x -0.05..0.05, y 0.95..1.05)만 점유. origin 을 돌려도 같은 곳이 점유된다."""
    n, res, a = 80, 0.05, math.radians(origin_yaw_deg)
    ox, oy = -2 * math.cos(a) + 2 * math.sin(a), -2 * math.sin(a) - 2 * math.cos(a)   # 판 중심이 (0, 0)
    data = []
    for r in range(n):
        for c in range(n):
            lx, ly = (c + 0.5) * res, (r + 0.5) * res
            x, y = ox + math.cos(a) * lx - math.sin(a) * ly, oy + math.sin(a) * lx + math.cos(a) * ly
            data.append(100 if 0.9 <= x <= 1.1 and -0.1 <= y <= 0.1 or -0.05 <= x <= 0.05 and 0.95 <= y <= 1.05 else 0)
    g = grid("map", n, n, res, ox, oy, data)
    g["info"]["origin"]["orientation"] = quat(a)
    return g


class FakeBridge:
    """subscribe 받은 토픽만 정해진 주기로 흘리는 rosbridge 흉내. publish 는 기록한다."""

    def __init__(self, map_odom=(0, 0, 0), spin=0.0, scan_pts=2000, map_yaw_deg=0):
        self.subs, self.published = set(), []
        self.map_odom, self.spin, self.scan_pts, self.map_yaw = map_odom, spin, scan_pts, map_yaw_deg
        ready = threading.Event()
        self.loop = asyncio.new_event_loop()

        async def handler(ws, *_):
            sender = asyncio.ensure_future(self.stream(ws))
            try:
                async for text in ws:
                    m = json.loads(text)
                    op = m.get("op")
                    if op == "subscribe":
                        self.subs.add(m["topic"])
                    elif op == "unsubscribe":
                        self.subs.discard(m["topic"])
                    elif op == "publish":
                        self.published.append(m)
                    elif op == "call_service":
                        await ws.send(json.dumps({"op": "service_response", "id": m.get("id"), "service": m.get("service"),
                                                  "values": {"topics": [], "types": []}, "result": True}))
            finally:
                sender.cancel()

        async def serve():
            self.server = await websockets.serve(handler, "127.0.0.1", 0, max_size=None)
            self.port = self.server.sockets[0].getsockname()[1]
            ready.set()
            await self.server.wait_closed()

        threading.Thread(target=lambda: self.loop.run_until_complete(serve()), daemon=True).start()
        ready.wait(10)

    async def stream(self, ws):
        t0, k, static_sent = time.time(), 0, False
        fmap = flip_map(self.map_yaw)
        mx, my, myaw = self.map_odom
        while True:
            t = time.time() - t0
            yaw = self.spin * t
            out = []
            def pub(topic, msg):
                if topic in self.subs:
                    out.append({"op": "publish", "topic": topic, "msg": msg})
            hdr = {"frame_id": "odom", "stamp": {"sec": int(time.time()), "nanosec": 0}}
            cov = [0.0] * 36
            cov[0], cov[1], cov[7], cov[35] = 0.04, 0.01, 0.01, 0.02
            pub("/odom", {"header": hdr, "child_frame_id": "base_link",
                          "pose": {"pose": {"position": {"x": 0, "y": 0, "z": 0}, "orientation": quat(yaw)}, "covariance": cov},
                          "twist": {"twist": {"linear": {"x": 0, "y": 0, "z": 0}, "angular": {"x": 0, "y": 0, "z": self.spin}}}})
            if k % 2 == 0:   # 10 Hz
                n = self.scan_pts
                pub("/scan", {"header": {"frame_id": "base_link"}, "angle_min": -math.pi, "angle_increment": 2 * math.pi / n,
                              "range_min": 0.05, "range_max": 12.0, "scan_time": 0.1,
                              "ranges": [1.5 + 0.5 * math.sin(4 * i * 2 * math.pi / n + t) for i in range(n)],
                              "intensities": [float(i % 100) for i in range(n)]})
            if k % 4 == 0:   # 5 Hz
                tfs = {"transforms": [tf("odom", "base_link", 0, 0, yaw), tf("map", "odom", mx, my, myaw)]}
                pub("/tf", tfs)
                pub("/map_odom", {"transforms": [tf("map", "odom", mx, my, myaw)]})
                pub("/amcl_pose", {"header": {"frame_id": "map"}, "pose": {"pose": {"position": {"x": mx, "y": my, "z": 0},
                                   "orientation": quat(myaw)}, "covariance": [0.09 if i in (0, 7) else 0.03 if i == 35 else 0 for i in range(36)]}})
                pub("/local_costmap/published_footprint", {"header": {"frame_id": "odom"}, "polygon": {"points": [
                    {"x": 0.2, "y": 0.18, "z": 0}, {"x": 0.2, "y": -0.18, "z": 0}, {"x": -0.2, "y": -0.18, "z": 0}, {"x": -0.2, "y": 0.18, "z": 0}]}})
            if "/tf_static" in self.subs and not static_sent:
                static_sent = True
                pub("/tf_static", {"transforms": [tf("base_link", "laser", 0.05, 0, 0), tf("base_link", "imu_link", 0, 0, 0)]})
            if k % 20 == 0:   # 1 Hz
                pub("/map", fmap)
                pub("/global_costmap/costmap", grid("map", 40, 40, 0.1, -2, -2, [(i * 7) % 101 for i in range(1600)]))
                pub("/local_costmap/costmap", grid("odom", 20, 20, 0.05, -0.5, -0.5, [0 if i % 3 else 99 for i in range(400)]))
            for m in out:
                await ws.send(json.dumps(m))
            k += 1
            await asyncio.sleep(0.05)

    def close(self):
        self.loop.call_soon_threadsafe(self.server.close)


def run(tmp, name, srv, actions, quit_after=8, size="1400x900"):
    a = tmp / f"{name}.actions.json"
    a.write_text(json.dumps(actions, ensure_ascii=False), encoding="utf-8")
    state, png = tmp / f"{name}.json", tmp / f"{name}.png"
    cmd = ["cmd", "/c", str(RUN), "--url", f"ws://127.0.0.1:{srv.port}", "--settings", str(tmp / f"{name}.ini"),
           "--page", "0", "--size", size, *([] if "--with-model" in sys.argv else ["--no-model"]), "--actions", str(a),
           "--state", str(state), "--screenshot", str(png), "--log", str(tmp / f"{name}.log"),
           "--quit-after", str(quit_after), "--fail-on-warning"]
    p = subprocess.run(cmd, env=dict(os.environ, ROBOT_DASHBOARD_TEST="1"), timeout=180, capture_output=True)
    st = json.loads(state.read_text(encoding="utf-8")) if state.exists() else None
    print(f"[{name}] 종료 코드 {p.returncode}" + (f" · render {st['render']}" if st else ""))
    if st and st.get("warnings"):
        print("   warnings:", [w for w in st["warnings"] if "Direct3D 12" not in w][:5])
    return p.returncode, st, png


def main():
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="p8b_scene_", dir=r"E:\Temp" if os.path.isdir(r"E:\Temp") else None))
    print(f"임시 폴더 {tmp}")

    # (1) mapflip: map→odom 항등, 위에서 시점. 점유 셀(로봇 앞 +1 m)이 화면 오른쪽(+x)에 와야 한다.
    for yaw in (0, 90):
        srv = FakeBridge(map_yaw_deg=yaw)
        rc, st, png = run(tmp, f"mapflip{yaw}", srv, [{"t": 0.5, "set": "scene.cam", "value": "위에서"},
                                                 {"t": 0.5, "set": "scene.layerPanelOpen", "value": False},
                                                 {"t": 0.5, "set": "scene.showScan", "value": False},
                                                 {"t": 0.5, "set": "robot.mapWanted", "value": True}], size="1900x1000")
        srv.close()
        check(rc == 0, f"(mapflip origin yaw {yaw}°) 종료 코드 0")
        check(st is not None and st["scene"]["showMap"] is True and st["robot"]["hasMap"] is True,
              f"(mapflip origin yaw {yaw}°) robot.mapWanted=true → 지도 체크박스(scene.showMap)도 켜짐 · hasMap")
        if png.exists():
            img = Image.open(png).convert("RGB")
            w, h = img.size
            # 격자 원점(빨강 x 축 행 · 파랑 y 축 열의 교차점 = odom 원점 = 로봇)에 대해 검정(점유) 셀이 오른쪽 +1 m 에 있어야 한다.
            # 회색 지도 판(가로 4 m 가 다 보임)의 폭으로 m 당 픽셀을 구한다.
            px = img.load()
            # 3D 뷰가 있는 왼쪽 위 영역만 본다(아래 비상 정지 버튼의 빨강을 피함)
            vw, vh = int(w * 0.7), int(h * 0.75)
            rows = [sum(1 for x in range(0, vw, 2) if px[x, y][0] > 150 and px[x, y][1] < 90 and px[x, y][2] < 90) for y in range(vh)]
            cols = [sum(1 for y in range(0, vh, 2) if px[x, y][2] > 150 and px[x, y][0] < 90) for x in range(vw)]
            oy, ox = rows.index(max(rows)), cols.index(max(cols))
            def edge(step):   # 원점 행 40 px 위에서 회색 판이 끝나는 x (격자선 같은 짧은 끊김은 건너뜀)
                x, gap = ox, 0
                while 0 < x < w - 1 and gap < 12:
                    r, g, b = px[x, oy - 40]
                    gap = 0 if 140 < r < 235 and abs(r - b) < 15 else gap + 1
                    x += step
                return x - step * gap
            span = (edge(1) - edge(-1)) / 4
            pts = [(x, y) for y in range(oy - 200, oy + 200) for x in range(ox - 400, ox + 400)
                   if 0 <= x < w and 0 <= y < h and sum(px[x, y]) < 90 and math.hypot(x - ox, y - oy) > 0.45 * span]
            if pts and span:
                # 앞(+1, 0) · 왼쪽(0, +1) 표식 가운데 가까운 쪽으로 나눠 각각의 중심을 본다
                got = {}
                for (ex, ey), name in (((1, 0), "앞"), ((0, 1), "왼쪽")):
                    near = [p for p in pts if math.hypot((p[0] - ox) / span - ex, -(p[1] - oy) / span - ey) < 0.4]
                    got[name] = ((sum(p[0] for p in near) / len(near) - ox) / span,
                                 -(sum(p[1] for p in near) / len(near) - oy) / span) if near else None
                print(f"   원점 ({ox}, {oy}) px · {span:.0f} px/m · 표식 중심(m) {got}")
                ok = all(got[n] and math.hypot(got[n][0] - e[0], got[n][1] - e[1]) < 0.15
                         for n, e in (("앞", (1, 0)), ("왼쪽", (0, 1))))
                check(ok, f"(mapflip origin yaw {yaw}°) 점유 셀이 로봇 앞 +1 m(화면 오른쪽), 표식이 +y(화면 위)")
            else:
                check(False, f"(mapflip origin yaw {yaw}°) 점유 셀 · 지도 판을 찾음")

    # (2) layers: 잔상 10 s · 거리 색 · 코스트맵 2 · footprint · TF · AMCL 켜기, map→odom (1, 2, 30°)
    srv = FakeBridge(map_odom=(1, 2, math.radians(30)))
    on = [{"t": 0.5, "set": f"scene.{k}", "value": v} for k, v in
          [("cam", "위에서"), ("scanDecay", 10), ("scanColorMode", "range"), ("showGlobalCostmap", True),
           ("showLocalCostmap", True), ("showFootprint", True), ("showTf", True), ("showAmcl", True), ("showOdomCov", True),
           ("trailGradient", True), ("showMap", True)]]
    rc, st, png = run(tmp, "layers", srv, on + [{"t": 3, "call": "robot.setInitialPose", "args": [0.5, 0.0, 0.0]}],
                      quit_after=14)
    srv.close()
    check(rc == 0, "(layers) 종료 코드 0")
    sc = st["scene"] if st else {}
    check(sc.get("costmapReceived") is True, "(layers) costmapReceived")
    check(sc.get("footprintReceived") is True, "(layers) footprintReceived")
    check(sc.get("amclReceived") is True, "(layers) amclReceived")
    check(sc.get("tfFrameCount", 0) >= 4, f"(layers) tfFrameCount >= 4 ({sc.get('tfFrameCount')})")
    check(sc.get("scanAccumPoints", 0) >= 18000, f"(layers) 잔상 10 s 점 >= 18000 ({sc.get('scanAccumPoints')})")
    check(st is not None and st["render"]["fpsMin"] > 0, f"(layers) fpsMin > 0 ({st and st['render']})")
    ip = [m for m in srv.published if m.get("topic") == "/dashboard/initialpose"]
    check(bool(ip) and ip[-1]["msg"]["header"]["frame_id"] == "map", "(initialpose) map 프레임으로 보냄")
    if ip:
        c = ip[-1]["msg"]["pose"]["covariance"]
        check(c[0] == 0.25 and c[7] == 0.25 and abs(c[35] - 0.0685) < 1e-3, "(initialpose) 공분산 [0]·[7]=0.25, [35]≈0.0685")

    # (3) follow: 3인칭, 로봇 회전 중(state 의 robot · scene 은 30 Hz 묶음 사이 시점이 달라 3° 까지 허용)
    srv = FakeBridge(spin=0.5)
    rc, st, png = run(tmp, "follow", srv, [{"t": 0.5, "set": "scene.cam", "value": "3인칭"},
                                            {"t": 0.5, "set": "scene.tpYaw", "value": 20}], quit_after=6)
    srv.close()
    check(rc == 0, "(follow) 종료 코드 0")
    check(st is not None and abs(st["scene"]["tpYaw"] - 20) < 1e-6 and st["scene"]["cam"] == "3인칭",
          "(follow) 3인칭 · 오프셋 유지")
    if st:
        th = st["robot"]["theta"] * 180 / math.pi
        vy = st["scene"]["viewYaw"]
        check(abs(((vy - (th - 90 + 20)) + 180) % 360 - 180) < 3, f"(follow) viewYaw = θ - 90 + tpYaw ({vy:.1f}, θ {th:.1f})")

    print("통과" if not FAILS else f"실패 {len(FAILS)}개: {FAILS}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
