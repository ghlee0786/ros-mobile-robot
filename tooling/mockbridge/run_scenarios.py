"""시나리오 실행기: scenarios/*.json 마다 모의 서버와 대시보드를 짝지어 띄우고 결과를 판정한다.

    UV_CACHE_DIR=/e/Temp/uv-cache uv run --no-project --with "websockets>=12,<14" --with websocket-client \\
        --with pillow python tooling/mockbridge/run_scenarios.py [--list] [--only normal,estop] [--jobs 2]

- 서버는 sys.executable 로 띄운다(같은 uv 환경). 포트는 서버가 고른 빈 포트(--port 를 주면 그 포트부터).
- 대시보드 인자(P2 시험 훅 약속):
    <exe> --url ws://127.0.0.1:<port> --settings <dir>/settings.ini --size 1440x900 --actions <dir>/actions.json
          --screenshot <dir>/shot.png --state <dir>/state.json --log <dir>/dash.log --quit-after <duration_s>
          [--no-model] <dashboard_args>
  환경변수 ROBOT_DASHBOARD_TEST=1 을 주고 PATH 앞에 E:\\Qt\\6.8.3\\mingw_64\\bin 을 넣는다.
  종료 코드: 0 정상, 1 QML 로드 실패, 2 --fail-on-warning 경고, 3 actions 실패.
- 결과는 E:\\Temp\\dashtest\\<시각>\\<id>\\ 에 모이고 summary.json 과 콘솔 표를 낸다. 모두 합격이면 종료 코드 0.
- 대시보드 exe 가 옆 소스(<exe>/../../src · qml)보다 오래됐으면 실행하지 않고 종료 코드 2 로 끝난다(다시 빌드).

시나리오 형식(JSON):
  {"id","title","needs_pi","needs_human","requires":["P4-core"],"server":true,"dashboards":1,"scene":false,
   "server_args":[],"dashboard_args":[],"duration_s":20,"baseline":"기준.png",
   "actions":[{"t":1,"call":"robot.drive","args":[1,0]}, {"t":2,"set":"scene.cam","value":"위에서"},
              {"t":3,"shot":"a.png"}, {"t":5,"runner":"kill_dashboard"}, {"t":6,"runner":"restart_server"}],
   "expect":{"exit_code":0,
             "state":[{"path":"robot.missingRequired","op":"==","value":[]}],
             "trace":[{"rule":"was_true_between"(또는 never_between),"path":"robot.linkLost","op":"==","value":true,"from_s":5,"to_s":9},
                      {"rule":"final_eq","path":"bridge.connected","value":true}],
             "server_log":[{"rule":"max_abs","topic":"/teleop/cmd_vel","field":"linear.x","value":0.1}, ...],
             "log_forbid":["TypeError"], "log_regex_max":[{"regex":"gui_ms=([0-9.]+)","max":50}],
             "mem_growth_max_mb":50, "mem_warmup_s":30}}
  state op 는 compare() 의 것 + exists · missing. trace 의 t 는 서버의 첫 연결 기준 초로 바꿔 판정한다(장애 시각도 첫 연결 기준).
  server_args · dashboard_args · actions 의 문자열 값(shot · value · args 안의 문자열) 안의 {here}(이 폴더) ·
  {out}(시나리오 결과 폴더, 대시보드 둘째부터는 그 dash<i> 폴더) · {pi_host} 는 바꿔 넣는다.
  server_log 규칙: max_abs · count_ge · count_le · within_ms_after_action · zero_after_action · throttle_eq ·
  rate_between · resubscribe_within · goal_inverse (각 함수 docstring 참고).
"""
import argparse
import concurrent.futures as cf
import datetime
import glob
import json
import math
import os
import re
import shlex
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MOCK = os.path.join(HERE, "mock_rosbridge.py")
QT_BIN = r"E:\Qt\6.8.3\mingw_64\bin"
DEFAULT_EXE = os.path.normpath(os.path.join(HERE, "..", "..", "dashboard", "build", "robot_dashboard.exe"))


# ---- 시나리오 읽기 ---------------------------------------------------------------------------
def load_scenarios(folder):
    out = []
    for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
        with open(path, encoding="utf-8") as f:
            sc = json.load(f)
        sc.setdefault("id", os.path.splitext(os.path.basename(path))[0])
        out.append(sc)
    return out


def skip_reason(sc, a):
    if a.only and sc["id"] not in a.only:
        return "only 아님"
    if sc.get("needs_pi") and (a.offline or not a.pi_host):
        return "파이 필요(--offline 또는 --pi-host 없음)"
    if sc.get("needs_human"):
        return "사람 필요"
    if a.skip_requires is not None and sc.get("requires"):
        if a.skip_requires == "*" or set(sc["requires"]) & set(a.skip_requires.split(",")):
            return "requires " + ",".join(sc["requires"])
    return None


def subst(args, **kw):
    out = []
    for x in args:
        x = str(x)
        for k, v in kw.items():
            x = x.replace("{" + k + "}", v)
        out.append(x)
    return out


def subst_action(x, **kw):
    out = {k: subst([v], **kw)[0] if isinstance(v, str) else v for k, v in x.items()}
    if isinstance(out.get("args"), list):
        out["args"] = [subst([v], **kw)[0] if isinstance(v, str) else v for v in out["args"]]
    return out


def stale_exe(exe):
    """exe 가 옆 소스(<exe>/../../src · qml)의 가장 새 파일보다 오래됐으면 그 파일 경로, 아니면 None."""
    root = os.path.dirname(os.path.dirname(os.path.abspath(exe)))
    files = [f for d in ("src", "qml") for f in glob.glob(os.path.join(root, d, "**", "*"), recursive=True)
             if os.path.isfile(f)]
    newest = max(files, key=os.path.getmtime, default=None)
    return newest if newest and os.path.getmtime(newest) > os.path.getmtime(exe) else None


# ---- 값 꺼내기 · 비교 ------------------------------------------------------------------------
def get_path(obj, path):
    cur = obj
    for part in path.split("."):
        if isinstance(cur, dict) and part in cur:
            cur = cur[part]
        elif isinstance(cur, list) and part.lstrip("-").isdigit():
            cur = cur[int(part)]
        elif isinstance(cur, list) and part == "count":
            cur = len(cur)
        else:
            raise KeyError(path)
    return cur


def compare(actual, op, value):
    if op == "==":
        return actual == value
    if op == "!=":
        return actual != value
    if op in ("<", "<=", ">", ">="):
        if not isinstance(actual, (int, float)) or isinstance(actual, bool):
            return False
        return {"<": actual < value, "<=": actual <= value, ">": actual > value, ">=": actual >= value}[op]
    if op == "contains":
        return value in actual
    if op == "not_contains":
        return value not in actual
    if op == "abs<=":
        return abs(actual) <= value
    raise ValueError(f"모르는 비교 {op}")


def check_state(state, rules):
    fails = []
    for r in rules:
        try:
            actual = get_path(state, r["path"])
        except (KeyError, IndexError, TypeError):
            if r.get("op") == "missing":
                continue
            fails.append(f"state {r['path']}: 없음")
            continue
        if r.get("op") == "exists":
            continue
        if r.get("op") == "missing":
            fails.append(f"state {r['path']}: 없어야 하는데 있음(실제 {actual!r})")
            continue
        if not compare(actual, r["op"], r["value"]):
            fails.append(f"state {r['path']} {r['op']} {r['value']!r} 아님(실제 {actual!r})")
    return fails


def check_trace(trace, rules):
    """trace: [{"t":초, ...상태}] (state.json 의 "trace" 또는 trace.jsonl)."""
    fails = []
    for r in rules:
        rule = r["rule"]
        if rule in ("was_true_between", "never_between"):
            ok = False
            for s in trace:
                if r.get("from_s", -1e9) <= s.get("t", 0) <= r.get("to_s", 1e9):
                    try:
                        ok |= compare(get_path(s, r["path"]), r.get("op", "=="), r.get("value", True))
                    except (KeyError, IndexError, TypeError):
                        pass
            if rule == "was_true_between" and not ok:
                fails.append(f"trace {r['path']} 가 {r.get('from_s')}..{r.get('to_s')} s 에 한 번도 {r.get('value', True)!r} 아님")
            elif rule == "never_between" and ok:
                fails.append(f"trace {r['path']} 가 {r.get('from_s')}..{r.get('to_s')} s 에 {r.get('value', True)!r} 인 적이 있음")
        elif rule == "final_eq":
            try:
                actual = get_path(trace[-1], r["path"]) if trace else None
            except (KeyError, IndexError, TypeError):
                actual = None
            if actual != r["value"]:
                fails.append(f"trace 마지막 {r['path']} == {r['value']!r} 아님(실제 {actual!r})")
        else:
            fails.append(f"모르는 trace 규칙 {rule}")
    return fails


def rebase_trace(trace, wall0):
    """trace 의 t 를 서버의 첫 연결(wall0, epoch) 기준 초로 바꾼다 — 모의 서버의 장애 시각(--freeze 6@5 등)과 같은 기준.
    wall0 이나 wall 이 없으면 그대로(대시보드 QML 로드 기준)."""
    if wall0 is None:
        return trace
    return [dict(s, t=s["wall"] - wall0) if "wall" in s else s for s in trace]


def mem_growth(mem, warmup_s):
    """mem: [(경과 s, MB)]. 시작 warmup_s 동안(QML 로드 · 첫 지도 등)은 빼고, 남은 표본 앞 1/5 의 최소에서 마지막까지 증가."""
    after = [m for t, m in mem if t >= warmup_s]
    if len(after) < 2:
        return None
    return after[-1] - min(after[: max(1, len(after) // 5)])


# ---- 서버 로그 규칙 --------------------------------------------------------------------------
def read_log(path):
    recs = []
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                try:
                    recs.append(json.loads(line))
                except ValueError:
                    pass
    return recs


def _match(rec, r, default_op="publish"):
    return (rec.get("dir", "rx") == r.get("dir", "rx") and rec.get("op") == r.get("op", default_op)
            and ("topic" not in r or rec.get("topic") == r["topic"])
            and ("service" not in r or rec.get("service") == r["service"])
            and all(_field(rec.get("msg", {}), k) == v for k, v in r.get("where", {}).items()))


def _field(msg, path):
    try:
        return get_path(msg, path)
    except (KeyError, IndexError, TypeError):
        return None


def _action_wall(actions_done, r):
    """state.json 의 actions_done[] 에서 r["action"](call 이름 또는 순번)의 wall(epoch 초)."""
    key = r.get("action")
    for i, a in enumerate(actions_done or []):
        if key == i or key == a.get("call") or key == a.get("set") or key == a.get("runner"):
            return a.get("wall")
    return None


def check_server_log(recs, rules, actions_done=None):
    fails = []
    t0 = next((x["t"] for x in recs if x.get("op") == "connect"), recs[0]["t"] if recs else 0)
    for r in rules:
        rule = r["rule"]
        if rule == "max_abs":
            # topic 의 rx publish 마다 |msg.field| <= value
            vals = [_field(x.get("msg", {}), r["field"]) for x in recs if _match(x, r)]
            bad = [v for v in vals if isinstance(v, (int, float)) and abs(v) > r["value"] + 1e-9]
            if bad:
                fails.append(f"{r['topic']} {r['field']} 최대 {max(map(abs, bad))} > {r['value']}")
        elif rule in ("count_ge", "count_le"):
            # op(기본 publish) · topic · where 가 맞는 기록 수
            n = sum(1 for x in recs if _match(x, r))
            if (rule == "count_ge" and n < r["value"]) or (rule == "count_le" and n > r["value"]):
                fails.append(f"{rule} {r.get('op', 'publish')} {r.get('topic', r.get('service', ''))}: {n} (기준 {r['value']})")
        elif rule == "within_ms_after_action":
            # action 뒤 ms 안에 맞는 기록이 min_count 개 이상. wall(epoch)끼리 비교한다.
            w = _action_wall(actions_done, r)
            if w is None:
                fails.append(f"within_ms_after_action: actions_done 에 {r.get('action')!r} 없음")
                continue
            n = sum(1 for x in recs if _match(x, r) and w <= x["wall"] <= w + r["ms"] / 1000.0)
            if n < r.get("min_count", 1):
                fails.append(f"{r.get('action')} 뒤 {r['ms']} ms 안 {r.get('op', 'publish')} {r.get('topic', r.get('service', ''))}"
                             f" {n} 개 (기준 {r.get('min_count', 1)})")
        elif rule == "zero_after_action":
            # action + after_ms 뒤로(until_action 이 있으면 그 action 전까지) topic 의 fields 가 모두 0 이어야 한다(비0 명령 없음)
            w = _action_wall(actions_done, r)
            if w is None:
                fails.append(f"zero_after_action: actions_done 에 {r.get('action')!r} 없음")
                continue
            until = _action_wall(actions_done, {"action": r["until_action"]}) if "until_action" in r else math.inf
            if until is None:
                fails.append(f"zero_after_action: actions_done 에 {r['until_action']!r} 없음")
                continue
            def nonzero(x):
                return any(abs(_field(x.get("msg", {}), f) or 0) > 1e-9 for f in r["fields"])
            # action 직전에 보낸 명령은 action 의 첫 0 명령과 같은 TCP 묶음으로 와 wall 이 action 보다 늦을 수 있다.
            # wall >= action 이어도 스트림 순서로 그 첫 0 명령보다 앞선 것은 action 전에 보낸 것이다.
            first_zero = next((i for i, x in enumerate(recs) if _match(x, r) and x["wall"] >= w and not nonzero(x)), None)
            bad = [x for i, x in enumerate(recs) if _match(x, r) and w + r.get("after_ms", 0) / 1000.0 < x["wall"] < until
                   and nonzero(x) and not (first_zero is not None and x["wall"] >= w and i < first_zero)]
            if bad:
                fails.append(f"{r.get('action')} 뒤 {r['topic']} 비0 명령 {len(bad)} 개")
        elif rule == "throttle_eq":
            # 그 토픽의 마지막(all 이면 모든) subscribe 의 throttle_rate == value
            subs = [x for x in recs if x.get("dir", "rx") == "rx" and x.get("op") == "subscribe" and x.get("topic") == r["topic"]]
            if not subs:
                fails.append(f"throttle_eq {r['topic']}: subscribe 없음")
                continue
            check = subs if r.get("all") else subs[-1:]
            bad = [x.get("throttle_rate", 0) for x in check if (x.get("throttle_rate") or 0) != r["value"]]
            if bad:
                fails.append(f"throttle_eq {r['topic']}: {bad} != {r['value']}")
        elif rule == "rate_between":
            # 첫 연결 기준 from_s..to_s 동안 맞는 기록의 초당 수가 min_hz..max_hz
            a, b = r.get("from_s", 0), r.get("to_s")
            sel = [x for x in recs if _match(x, r) and x["t"] - t0 >= a and (b is None or x["t"] - t0 <= b)]
            if b is None:
                b = (sel[-1]["t"] - t0) if sel else a
            hz = len(sel) / max(1e-6, b - a)
            if not r["min_hz"] <= hz <= r["max_hz"]:
                fails.append(f"rate_between {r.get('topic')}: {hz:.2f} Hz (기준 {r['min_hz']}..{r['max_hz']})")
        elif rule == "resubscribe_within":
            # 다시 붙을 때마다 within_s 안에 처음과 같은 subscribe 집합과 advertise min_advertise 개
            conns = [x for x in recs if x.get("op") == "connect"]
            if len(conns) < r.get("min_reconnects", 1) + 1:
                fails.append(f"재연결 {len(conns) - 1} 번 (기준 {r.get('min_reconnects', 1)})")
                continue

            def session(c):
                sel = [x for x in recs if x.get("conn") == c["conn"] and x.get("dir") == "rx" and x["t"] - c["t"] <= r["within_s"]]
                return ({x["topic"] for x in sel if x.get("op") == "subscribe"},
                        sum(1 for x in sel if x.get("op") == "advertise"))
            first, _ = session(conns[0])
            for c in conns[1:]:
                subs, adv = session(c)
                if subs != first or adv < r.get("min_advertise", 4):
                    fails.append(f"연결 {c['conn']}: {r['within_s']} s 안 subscribe 차이 {sorted(first ^ subs)}, advertise {adv}")
        elif rule == "goal_inverse":
            # topic(기본 /goal_pose, PoseWithCovarianceStamped 도 됨)의 마지막 rx 가 frame 이고,
            # map_odom(x,y,yaw_deg) 역변환이 odom(x,y,yaw_rad) 과 tol_m · tol_deg 안
            topic = r.get("topic", "/goal_pose")
            goals = [x for x in recs if _match(x, {"topic": topic})]
            if not goals:
                fails.append(f"goal_inverse: {topic} 없음")
                continue
            m = goals[-1]["msg"]
            if m.get("header", {}).get("frame_id") != r.get("frame", "map"):
                fails.append(f"{topic} 프레임 {m.get('header', {}).get('frame_id')!r} != {r.get('frame', 'map')!r}")
                continue
            mx, my, myaw = r["map_odom"][0], r["map_odom"][1], math.radians(r["map_odom"][2])
            pose = m["pose"].get("pose", m["pose"])
            p, q = pose["position"], pose["orientation"]
            dx, dy, c, s = p["x"] - mx, p["y"] - my, math.cos(myaw), math.sin(myaw)
            ox, oy = c * dx + s * dy, -s * dx + c * dy
            oyaw = math.atan2(2 * (q["w"] * q["z"] + q["x"] * q["y"]), 1 - 2 * (q["y"] ** 2 + q["z"] ** 2)) - myaw
            ex, ey, eyaw = r["odom"]
            dyaw = math.degrees(math.atan2(math.sin(oyaw - eyaw), math.cos(oyaw - eyaw)))
            if math.hypot(ox - ex, oy - ey) > r.get("tol_m", 0.02) or abs(dyaw) > r.get("tol_deg", 5):
                fails.append(f"goal_inverse: odom ({ox:.3f},{oy:.3f},{math.degrees(oyaw):.1f}°) != {r['odom']}")
        else:
            fails.append(f"모르는 server_log 규칙 {rule}")
    return fails


def check_dash_log(text, expect):
    fails = []
    for word in expect.get("log_forbid", []):
        if word in text:
            fails.append(f"로그 금지어 {word!r}")
    for r in expect.get("log_regex_max", []):
        vals = [float(m.group(1)) for m in re.finditer(r["regex"], text)]
        if vals and max(vals) > r["max"]:
            fails.append(f"로그 {r['regex']} 최대 {max(vals)} > {r['max']}")
        if not vals and r.get("required"):
            fails.append(f"로그 {r['regex']} 없음")
    return fails


# ---- 프로세스 ----------------------------------------------------------------------------
# 띄운 서버 · 대시보드. 실행기가 예외 · Ctrl+C 로 끊겨도 main 이 남은 것을 모두 끝낸다(창 · 서버를 남기지 않는다).
LIVE = set()
_live_lock = threading.Lock()
_stopping = threading.Event()


def track(proc):
    with _live_lock:
        LIVE.add(proc)
    if _stopping.is_set():   # 정리가 시작된 뒤 일꾼 스레드가 띄운 것
        kill_tree(proc)
    return proc


def kill_tree(proc):
    """프로세스와 자식까지 끝낸다(uv 가상환경 python.exe 는 진짜 python 을 자식으로 띄우는 런처다)."""
    if proc.poll() is None:
        if os.name == "nt":
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], capture_output=True)
        else:
            proc.kill()
    try:
        proc.wait(5)
    except subprocess.TimeoutExpired:
        pass
    with _live_lock:
        LIVE.discard(proc)


def kill_all():
    _stopping.set()
    with _live_lock:
        procs = list(LIVE)
    for p in procs:
        kill_tree(p)


class ServerProc:
    def __init__(self, sdir, args, port):
        self.sdir, self.args, self.port, self.proc = sdir, args, port, None

    def start(self, timeout=30):
        out = open(os.path.join(self.sdir, "server.out"), "a", encoding="utf-8")
        cmd = [sys.executable, MOCK, "--port", str(self.port), "--log", os.path.join(self.sdir, "server.jsonl"),
               "--log-tx", *self.args]
        self.proc = track(subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=out, text=True, encoding="utf-8"))
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.proc.stdout.readline()
            if not line:
                break
            if line.startswith("MOCKBRIDGE_PORT="):
                self.port = int(line.strip().split("=")[1])
                threading.Thread(target=self._drain, args=(out,), daemon=True).start()
                return self.port
        raise RuntimeError(f"모의 서버가 뜨지 않음: {' '.join(cmd)}")

    def _drain(self, out):
        for line in self.proc.stdout:
            out.write(line)

    def stop(self):
        if self.proc:
            kill_tree(self.proc)


def dashboard_cmd(a, sc, ddir, port):
    base = [s.strip('"') for s in shlex.split(a.dashboard_cmd, posix=False)] if a.dashboard_cmd else [a.exe]
    extra = subst(sc.get("dashboard_args", []), here=HERE, out=ddir, pi_host=a.pi_host or "")
    cmd = list(base)
    if "--url" not in extra:
        cmd += ["--url", f"ws://127.0.0.1:{port}"]
    cmd += ["--settings", os.path.join(ddir, "settings.ini"), "--size", "1440x900",
            "--actions", os.path.join(ddir, "actions.json"), "--screenshot", os.path.join(ddir, "shot.png"),
            "--state", os.path.join(ddir, "state.json"), "--log", os.path.join(ddir, "dash.log"),
            "--quit-after", str(sc.get("duration_s", 20))]
    if sc.get("expect", {}).get("trace"):
        cmd += ["--trace", os.path.join(ddir, "trace.jsonl")]
    if not sc.get("scene"):
        cmd.append("--no-model")
    return cmd + extra


def dashboard_env():
    env = dict(os.environ, ROBOT_DASHBOARD_TEST="1")
    env["PATH"] = QT_BIN + os.pathsep + env.get("PATH", "")
    return env


def mem_mb(pid):
    """Windows tasklist 로 작업 집합(MB)."""
    try:
        out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV", "/NH"], capture_output=True,
                             text=True, timeout=10).stdout
        return float(re.sub(r"[^0-9]", "", out.strip().split('","')[-1])) / 1024.0
    except (OSError, ValueError, IndexError, subprocess.SubprocessError):
        return None


def run_one(sc, a, root, port):
    sid = sc["id"]
    sdir = os.path.join(root, sid)
    os.makedirs(sdir, exist_ok=True)
    res = {"id": sid, "title": sc.get("title", ""), "status": "pass", "failures": [], "warnings": [], "exit_codes": []}
    t_start = time.time()
    expect = sc.get("expect", {})
    duration = sc.get("duration_s", 20)
    server = None
    dashes = []
    runner_done = []
    mem = []
    try:
        if sc.get("server", True):
            server = ServerProc(sdir, subst(sc.get("server_args", []), here=HERE, out=sdir, pi_host=a.pi_host or ""), port)
            port = server.start()
        res["port"] = port
        n_dash = 0 if a.no_dashboard else sc.get("dashboards", 1)
        dash_actions = [x for x in sc.get("actions", []) if "runner" not in x]
        env = dashboard_env()
        for i in range(n_dash):
            ddir = sdir if i == 0 else os.path.join(sdir, f"dash{i}")
            os.makedirs(ddir, exist_ok=True)
            # 상대 shot 경로는 대시보드 작업 폴더(저장소 루트)가 아니라 결과 폴더에 쓴다
            acts = [subst_action(x, here=HERE, out=ddir, pi_host=a.pi_host or "") for x in dash_actions]
            acts = [dict(x, shot=os.path.join(ddir, x["shot"])) if "shot" in x and not os.path.isabs(x["shot"]) else x
                    for x in acts]
            with open(os.path.join(ddir, "actions.json"), "w", encoding="utf-8") as f:
                json.dump(acts, f, ensure_ascii=False, indent=1)
            cmd = dashboard_cmd(a, sc, ddir, port)
            with open(os.path.join(ddir, "cmd.txt"), "w", encoding="utf-8") as f:
                f.write(subprocess.list2cmdline(cmd))
            out = open(os.path.join(ddir, "dash.out"), "w", encoding="utf-8")
            dashes.append({"dir": ddir, "proc": track(subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=env)),
                           "killed": False})
        if a.no_dashboard:
            print(f"[{sid}] 서버 ws://127.0.0.1:{port} — {duration} s 동안 다른 클라이언트를 기다림", flush=True)
        # 실행기 동작 · 메모리 표본
        t0 = time.perf_counter()
        pending = sorted((x for x in sc.get("actions", []) if "runner" in x), key=lambda x: x["t"])
        next_mem = 0.0
        while True:
            el = time.perf_counter() - t0
            alive = [d for d in dashes if d["proc"].poll() is None]
            if (dashes and not alive and not pending) or el > duration + (30 if dashes else 0):
                break
            if not dashes and el >= duration:
                break
            while pending and pending[0]["t"] <= el:
                act = pending.pop(0)
                runner_done.append({**act, "wall": time.time()})
                if act["runner"] == "kill_dashboard":
                    for d in dashes:
                        if d["proc"].poll() is None:
                            kill_tree(d["proc"])
                            d["killed"] = True
                elif act["runner"] == "restart_server" and server:
                    server.stop()
                    time.sleep(act.get("down_s", 1.0))
                    server.start()
                else:
                    res["failures"].append(f"모르는 실행기 동작 {act['runner']}")
            if dashes and expect.get("mem_growth_max_mb") and el >= next_mem:
                m = mem_mb(dashes[0]["proc"].pid)
                if m:
                    mem.append((round(el, 1), m))
                next_mem = el + 10
            time.sleep(0.1)
        for d in dashes:
            if d["proc"].poll() is None:
                kill_tree(d["proc"])
                d["killed"] = True
                res["failures"].append(f"대시보드가 {duration} s + 30 s 안에 끝나지 않음")
            d["proc"].wait()
            res["exit_codes"].append(None if d["killed"] else d["proc"].returncode)
    except Exception as e:
        res["failures"].append(f"실행 오류: {e}")
    finally:
        for d in dashes:
            kill_tree(d["proc"])
        if server:
            server.stop()

    # ---- 판정 ----
    state = {}
    want_exit = expect.get("exit_code", 0)
    for i, d in enumerate(dashes):
        code = res["exit_codes"][i] if i < len(res["exit_codes"]) else None
        if code is not None and code != want_exit:
            res["failures"].append(f"대시보드{i} 종료 코드 {code} (기대 {want_exit})")
        sp = os.path.join(d["dir"], "state.json")
        st = {}
        if os.path.exists(sp):
            with open(sp, encoding="utf-8") as f:
                st = json.load(f)
        elif not d["killed"]:
            res["failures"].append(f"대시보드{i} state.json 없음")
        if i == 0:
            state = st
        rules = [r for r in expect.get("state", []) if r.get("dash", 0) == i]
        if st or rules:
            res["failures"] += check_state(st, rules)
        if expect.get("trace") and i == 0:
            trace = st.get("trace") or read_log(os.path.join(d["dir"], "trace.jsonl"))
            if not trace:
                res["failures"].append("trace 없음")
            conn0 = next((x["wall"] for x in read_log(os.path.join(sdir, "server.jsonl")) if x.get("op") == "connect"), None)
            res["failures"] += check_trace(rebase_trace(trace, conn0), expect["trace"])
        lp = os.path.join(d["dir"], "dash.log")
        if os.path.exists(lp):
            with open(lp, encoding="utf-8", errors="replace") as f:
                res["failures"] += check_dash_log(f.read(), expect)
    if server and expect.get("server_log"):
        actions_done = (state.get("actions_done") or []) + runner_done
        res["failures"] += check_server_log(read_log(os.path.join(sdir, "server.jsonl")), expect["server_log"], actions_done)
    growth = mem_growth(mem, expect.get("mem_warmup_s", 30)) if expect.get("mem_growth_max_mb") else None
    if growth is not None:
        res["mem_mb"] = mem
        res["mem_growth_mb"] = round(growth, 1)
        if growth > expect["mem_growth_max_mb"]:
            res["failures"].append(f"메모리 증가 {growth:.0f} MB > {expect['mem_growth_max_mb']} MB")
    shot = os.path.join(sdir, "shot.png")
    if sc.get("baseline") and os.path.exists(shot):
        try:
            import shot_diff
            base = os.path.join(HERE, "scenarios", sc["baseline"])
            if os.path.exists(base):
                ratio = shot_diff.diff_ratio(base, shot)
                res["shot_diff"] = ratio
                if ratio > sc.get("baseline_max", 0.02):
                    res["warnings"].append(f"스크린샷 차이 {ratio:.1%} (참고)")
        except ImportError:
            res["warnings"].append("Pillow 없음: 스크린샷 비교 건너뜀")
    if res["failures"]:
        res["status"] = "fail"
    res["secs"] = round(time.time() - t_start, 1)
    with open(os.path.join(sdir, "result.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    return res


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--list", action="store_true", help="시나리오 목록만 보인다")
    p.add_argument("--only", type=lambda s: s.split(","), help="id1,id2 만")
    p.add_argument("--offline", action="store_true", help="needs_pi 시나리오를 건너뛴다")
    p.add_argument("--skip-requires", nargs="?", const="*", help="requires 가 있는(또는 주어진 태그의) 시나리오를 건너뛴다")
    p.add_argument("--jobs", type=int, default=2, help="동시에 돌릴 시나리오 수 (기본 2)")
    p.add_argument("--no-dashboard", action="store_true", help="서버만 띄운다(다른 클라이언트로 시험)")
    p.add_argument("--port", type=int, default=0, help="서버 포트(0 이면 빈 포트, 주면 시나리오마다 +1)")
    p.add_argument("--pi-host", help="needs_pi 시나리오가 붙을 파이 주소")
    p.add_argument("--exe", default=DEFAULT_EXE, help="대시보드 실행 파일")
    p.add_argument("--dashboard-cmd", help="대시보드 대신 쓸 명령(예: \"python tests/fake_dashboard.py\")")
    p.add_argument("--scenarios", default=os.path.join(HERE, "scenarios"), help="시나리오 폴더")
    p.add_argument("--out", help=r"결과 폴더 (기본 E:\Temp\dashtest\<시각>)")
    a = p.parse_args(argv)

    scs = load_scenarios(a.scenarios)
    if a.list:
        for sc in scs:
            tags = [t for t, on in (("pi", sc.get("needs_pi")), ("human", sc.get("needs_human"))) if on]
            tags += sc.get("requires", [])
            print(f"{sc['id']:<16} {sc.get('duration_s', 20):>4}s  {sc.get('title', '')}" + (f"  [{', '.join(tags)}]" if tags else ""))
        return 0
    if not (a.no_dashboard or a.dashboard_cmd or os.path.exists(a.exe)):
        print(f"대시보드 실행 파일 없음: {a.exe} (--exe · --dashboard-cmd · --no-dashboard)", file=sys.stderr)
        return 2
    if not (a.no_dashboard or a.dashboard_cmd) and (newer := stale_exe(a.exe)):
        print(f"대시보드 실행 파일이 소스보다 오래됨: {a.exe} < {newer} — dashboard/build.cmd 로 다시 빌드한다",
              file=sys.stderr)
        return 2
    root = a.out or os.path.join(r"E:\Temp\dashtest", datetime.datetime.now().strftime("%Y%m%d_%H%M%S"))
    os.makedirs(root, exist_ok=True)
    results, todo = [], []
    for sc in scs:
        why = skip_reason(sc, a)
        if why:
            if why != "only 아님":
                results.append({"id": sc["id"], "title": sc.get("title", ""), "status": "skip", "failures": [why]})
        else:
            todo.append(sc)
    ex = cf.ThreadPoolExecutor(max_workers=max(1, a.jobs))
    finished = False
    try:
        futs = [ex.submit(run_one, sc, a, root, a.port + i if a.port else 0) for i, sc in enumerate(todo)]
        results += [f.result() for f in futs]
        finished = True
    finally:   # 정상 종료면 이미 다 끝났고, 끊겼으면 아직 돌지 않은 시나리오를 버리고 띄운 것을 모두 끝낸다
        ex.shutdown(wait=False, cancel_futures=True)
        kill_all()
        if finished:
            _stopping.clear()
    results.sort(key=lambda r: r["id"])
    with open(os.path.join(root, "summary.json"), "w", encoding="utf-8") as f:
        json.dump({"root": root, "results": results}, f, ensure_ascii=False, indent=1)
    mark = {"pass": "합격", "fail": "불합격", "skip": "건너뜀"}
    for r in results:
        print(f"{mark[r['status']]:<4} {r['id']:<16} {r.get('secs', ''):>6}  {'; '.join(r['failures'] + r.get('warnings', []))}")
    n_fail = sum(r["status"] == "fail" for r in results)
    print(f"[요약] 합격 {sum(r['status'] == 'pass' for r in results)} · 불합격 {n_fail} · "
          f"건너뜀 {sum(r['status'] == 'skip' for r in results)} → {root}")
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
