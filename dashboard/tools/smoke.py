"""대시보드를 시험 모드로 띄워 N 초 동안 살아 있는지, 로그에 QML 오류가 없는지 본다.

    python -I dashboard/tools/smoke.py [--seconds 12] [--log 파일] [-- 대시보드 인자...]

ROBOT_DASHBOARD_TEST=1 로 띄우므로 실제 로봇에 자동 연결하지 않는다(main.cpp). N 초 전에 스스로 끝나면 실패.
종료 코드 0 = 통과.
"""
import argparse
import os
import pathlib
import re
import subprocess
import sys
import time

FORBIDDEN = re.compile(r"TypeError|ReferenceError|is not defined|is not a type|Binding loop|Cannot assign|"
                       r"Unable to assign|is not installed|failed to load|module .* not found|Cannot read property")


def main():
    here = pathlib.Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=12)
    ap.add_argument("--log", default=None)
    ap.add_argument("--exe", default=str(here / "build" / "robot_dashboard.exe"))
    ap.add_argument("args", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    extra = a.args[1:] if a.args[:1] == ["--"] else a.args
    qt = os.environ.get("QT_ROOT", r"E:\Qt")
    env = dict(os.environ, ROBOT_DASHBOARD_TEST="1", QT_FORCE_STDERR_LOGGING="1",
               PATH=os.pathsep.join([rf"{qt}\6.8.3\mingw_64\bin", rf"{qt}\Tools\mingw1310_64\bin", os.environ.get("PATH", "")]))
    log = pathlib.Path(a.log) if a.log else here / "build" / "smoke.log"
    with open(log, "wb") as f:
        p = subprocess.Popen([a.exe, *extra], cwd=here, env=env, stdout=f, stderr=subprocess.STDOUT)
        t0 = time.time()
        while time.time() - t0 < a.seconds and p.poll() is None:
            time.sleep(0.2)
        alive = p.poll() is None
        if alive:
            p.kill()
            p.wait()
    text = log.read_text(encoding="utf-8", errors="replace")
    bad = [ln for ln in text.splitlines() if FORBIDDEN.search(ln)]
    ok = alive and not bad
    print(f"[smoke] {'통과' if ok else '실패'}: {a.seconds:.0f}s 동안 {'살아 있음' if alive else f'먼저 종료(code {p.returncode})'}, "
          f"오류 줄 {len(bad)}개, 로그 {log}")
    for ln in bad[:20]:
        print("  " + ln)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
