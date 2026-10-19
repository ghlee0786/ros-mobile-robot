"""README 용 데모 GIF: 모의 서버(Nav2 흉내 포함) 위에서 대시보드가 목표로 주행하는 모습을 창 녹화로 만든다.

    UV_CACHE_DIR=E:/Temp/uv-cache uv run --no-project --with "websockets>=12,<14" \\
        python tooling/mockbridge/make_demo_gif.py [--out docs/assets] [--scenes 3d,top,inspector]

파이가 필요 없다. 필요한 것: dashboard/build/robot_dashboard.exe(먼저 dashboard\\build.cmd), PATH 의 ffmpeg.
대시보드 창 자리의 바탕화면을 ffmpeg gdigrab 으로 녹화하므로 화면이 켜져 있고 창이 가려지지 않아야 한다(시험 훅의
grabWindow 는 Quick 3D 에서 한 장에 약 1 s 이고, gdigrab 의 title= 로 창만 잡으면 GPU 로 그리는 창은 첫 화면에 멈춘다). 장면마다 <out>/demo_<장면>.gif 를 쓰고,
녹화 원본은 E:/Temp/demo_gif/<장면>/ 에 남는다.
"""
import argparse
import ctypes
import ctypes.wintypes
import datetime
import json
import os
import pathlib
import re
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
EXE = ROOT / "dashboard" / "build" / "robot_dashboard.exe"
WORK = pathlib.Path(os.environ.get("DEMO_GIF_WORK", "E:/Temp/demo_gif"))

LAYERS = [{"set": "scene.showMap", "value": True}, {"set": "scene.showLocalCostmap", "value": True},
          {"set": "scene.showFootprint", "value": True}, {"set": "scene.trailGradient", "value": True},
          {"set": "scene.scanDecay", "value": 1}]

# 장면: 준비 동작(QML 로드 1 s 뒤), 목표(odom m · rad, 없으면 None), 녹화에서 쓸 구간 길이(s)
SCENES = {
    "3d": {"prep": LAYERS + [{"set": "scene.cam", "value": "3인칭"}], "goal": [2.2, -1.1, -0.6], "secs": 12},
    "top": {"prep": LAYERS + [{"set": "scene.cam", "value": "위에서"}], "goal": [2.2, 1.0, 1.2], "secs": 12},
    "inspector": {"prep": [{"set": "win.page", "value": 1}], "goal": None, "secs": 6},
}
MARK_T = 3.0   # 목표를 보내는(또는 녹화 구간을 시작하는) 시각, QML 로드 기준 s


def start_server(log):
    cmd = [sys.executable, str(HERE / "mock_rosbridge.py"), "--port", "0", "--nav", "--map-odom", "0,0,0"]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=open(log, "w"), text=True, encoding="utf-8")
    t0 = time.time()
    while time.time() - t0 < 20:
        m = re.search(r"MOCKBRIDGE_PORT=(\d+)", p.stdout.readline() or "")
        if m:
            return p, int(m.group(1))
    p.kill()
    raise RuntimeError("모의 서버가 포트를 알리지 않았다")


def window_rect(pid, timeout=15):
    """프로세스 pid 의 보이는 최상위 창을 맨 위에 고정하고 화면 좌표(DWM 실제 테두리, 물리 픽셀) (x, y, w, h) 를 돌려준다.
    같은 제목의 다른 대시보드(동시에 도는 시험)가 있어도 이 프로세스의 창만 잡는다."""
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
    user32, dwm = ctypes.windll.user32, ctypes.windll.dwmapi
    found = []
    proc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)

    def each(hwnd, _):
        owner = ctypes.wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd) and user32.GetWindowTextLengthW(hwnd):
            found.append(hwnd)
        return True

    t0 = time.time()
    while time.time() - t0 < timeout:
        found.clear()
        user32.EnumWindows(proc(each), 0)
        if found:
            hwnd = found[0]
            user32.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)   # HWND_TOPMOST, NOSIZE|NOMOVE|SHOW
            time.sleep(0.3)
            r = ctypes.wintypes.RECT()
            dwm.DwmGetWindowAttribute(hwnd, 9, ctypes.byref(r), ctypes.sizeof(r))   # DWMWA_EXTENDED_FRAME_BOUNDS
            w, h = (r.right - r.left) // 2 * 2, (r.bottom - r.top) // 2 * 2
            return max(0, r.left), max(0, r.top), w, h
        time.sleep(0.3)
    raise RuntimeError(f"pid {pid} 의 창을 찾지 못했다")


def mark_time(log):
    """대시보드 로그에서 MARK_T 에 돈 동작(목표 전송 등)의 벽시계 시각."""
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        if "[action]" in line and f'"t":{MARK_T:g}' in line:
            hh, mm, ss = line[1:13].split(":")
            return int(hh) * 3600 + int(mm) * 60 + float(ss)
    return None


def run_scene(name, spec, size, fps):
    d = WORK / name
    d.mkdir(parents=True, exist_ok=True)
    actions = [{"wait": "bridge.connected", "timeout_s": 10}, {"t": 0.5, "call": "robot.armTeleop", "args": [True]}]
    actions += [dict(a, t=1.0) for a in spec["prep"]]
    actions.append({"t": MARK_T, "call": "robot.sendGoal", "args": spec["goal"]} if spec["goal"]
                   else {"t": MARK_T, "set": "win.page", "value": 1})
    (d / "actions.json").write_text(json.dumps(actions, ensure_ascii=False), encoding="utf-8")
    log = d / "dash.log"
    srv, port = start_server(d / "server.log")
    env = dict(os.environ, ROBOT_DASHBOARD_TEST="1",
               PATH=os.pathsep.join([r"E:\Qt\6.8.3\mingw_64\bin", os.environ.get("PATH", "")]))
    quit_after = MARK_T + spec["secs"] + 1
    dash = subprocess.Popen([str(EXE), "--url", f"ws://127.0.0.1:{port}", "--settings", str(d / "settings" / "s.ini"),
                             "--size", size, "--actions", str(d / "actions.json"), "--quit-after", str(quit_after),
                             "--log", str(log)], cwd=ROOT / "dashboard", env=env)
    raw = d / "raw.mp4"
    try:
        x, y, w, h = window_rect(dash.pid)
        time.sleep(0.5)
        now = datetime.datetime.now()
        rec_start = now.hour * 3600 + now.minute * 60 + now.second + now.microsecond / 1e6
        rec = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", str(fps),
                                "-draw_mouse", "0", "-offset_x", str(x), "-offset_y", str(y),
                                "-video_size", f"{w}x{h}", "-i", "desktop", "-t", str(quit_after + 8), "-c:v", "libx264", "-preset",
                                "ultrafast", "-crf", "18", "-pix_fmt", "yuv420p", str(raw)])
        dash.wait(timeout=quit_after + 60)
        rec.wait(timeout=quit_after + 30)
    finally:
        for p in (dash, srv):
            if p.poll() is None:
                p.kill()
    mt = mark_time(log)
    if mt is None or not raw.exists():
        return None, d
    return max(0.0, mt - rec_start - 0.3), d


def to_gif(raw, start, secs, out, width, fps):
    vf = f"fps={fps},scale={width}:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=160:stats_mode=diff[p];" \
         f"[b][p]paletteuse=dither=none:diff_mode=rectangle"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-ss", f"{start:.2f}", "-t", str(secs), "-i", str(raw),
                    "-vf", vf, "-loop", "0", str(out)], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "docs" / "assets"))
    ap.add_argument("--scenes", default="3d,top,inspector")
    ap.add_argument("--size", default="1280x800")
    ap.add_argument("--width", type=int, default=960)
    ap.add_argument("--fps", type=int, default=12)
    a = ap.parse_args()
    for name in a.scenes.split(","):
        spec = SCENES[name]
        start, d = run_scene(name, spec, a.size, 30)
        if start is None:
            print(f"[{name}] 녹화 실패 — {d} 확인")
            continue
        out = pathlib.Path(a.out) / f"demo_{name}.gif"
        to_gif(d / "raw.mp4", start, spec["secs"], out, a.width, a.fps)
        print(f"[{name}] {spec['secs']} s → {out} ({out.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
