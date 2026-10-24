"""설치 파일 시험(표준 라이브러리만): 무인 설치 → 설치본 실행 → 업그레이드 → 무인 제거.

python -I installer/test_installer.py   (먼저 installer\\build_installer.cmd 로 dist\\ROSMonitor-Setup-<ver>.exe 를 만든다)

- 사용자 설정은 건드리지 않는다: 설치 파일에 /SETTINGSKEY 로 시험용 키(HKCU\\Software\\RobotDashboardInstallerTest\\...)를 주고,
  설치본은 ROBOT_DASHBOARD_TEST=1(설정을 RobotDashboardTest 조직에 따로 씀)로 띄운다.
- 끝나면(실패해도) 무인 제거하고 시험용 키 · 폴더를 지워 시작 메뉴 · 제거 항목이 남지 않게 한다.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import winreg
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP_ID = "{B7D82307-ECDE-49A5-A9C4-80D2C7990238}"
UNINSTALL_KEY = rf"Software\Microsoft\Windows\CurrentVersion\Uninstall\{APP_ID}_is1"
TEST_ROOT = r"Software\RobotDashboardInstallerTest"
TEST_KEY = TEST_ROOT + r"\RobotDashboard"
INSTALL_DIR = Path(r"E:\Temp\rdinstall_test")
EXPECTED = ["robot_dashboard.exe", "Qt6Core.dll", "Qt6Quick3D.dll", "Qt6Graphs.dll", "Qt6WebSockets.dll", "Qt6Svg.dll",
            "platforms/qwindows.dll", "tls/qschannelbackend.dll", "imageformats/qsvg.dll", "qml/QtQuick3D",
            "model/body.glb", "model/wheel_left.glb", "model/wheel_right.glb", "model/lidar_head.glb", "model/parts.json",
            "unins000.exe"]


def check(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        raise SystemExit(f"실패: {what}")


def version():
    m = re.search(r"project\(robot_dashboard VERSION (\d+\.\d+\.\d+)", (ROOT / "dashboard/CMakeLists.txt").read_text(encoding="utf-8"))
    return m.group(1)


def reg_get(key, name):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key) as k:
            return winreg.QueryValueEx(k, name)[0]
    except FileNotFoundError:
        return None


def reg_delete_tree(root):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, root, 0, winreg.KEY_ALL_ACCESS) as k:
            while True:
                try:
                    sub = winreg.EnumKey(k, 0)
                except OSError:
                    break
                reg_delete_tree(root + "\\" + sub)
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, root)
    except FileNotFoundError:
        pass


def run_setup(setup, log):
    args = [str(setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER", f"/DIR={INSTALL_DIR}",
            f"/SETTINGSKEY={TEST_KEY}", f"/LOG={log}"]
    r = subprocess.run(args, timeout=600)
    check(r.returncode == 0, f"설치 종료 코드 0 (받은 값 {r.returncode}, 로그 {log})")


def uninstall():
    unins = INSTALL_DIR / "unins000.exe"
    if not unins.exists():
        return
    subprocess.run([str(unins), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"], timeout=300)
    # 제거 프로그램은 임시 폴더에 자기를 복사해 돌리고 바로 끝난다 — 파일이 지워질 때까지 기다린다
    deadline = time.time() + 180
    while time.time() < deadline and (unins.exists() or (INSTALL_DIR / "robot_dashboard.exe").exists()):
        time.sleep(1)
    time.sleep(2)


def smoke_run(tmp):
    """설치본을 시험 모드로 10 s 띄워 QML 오류 없이 끝나는지 본다(Qt 는 설치 폴더의 것만 쓰게 PATH 에서 뺀다)."""
    env = dict(os.environ, ROBOT_DASHBOARD_TEST="1")
    env.pop("ROBOT_DASHBOARD_UPDATE_URL", None)
    env["PATH"] = os.pathsep.join(p for p in env.get("PATH", "").split(os.pathsep) if "qt" not in p.lower())
    state = Path(tmp) / "state.json"
    r = subprocess.run([str(INSTALL_DIR / "robot_dashboard.exe"), "--no-autoconnect", "--no-publish", "--quit-after", "10",
                        "--fail-on-warning", "--state", str(state)], env=env, timeout=120)
    check(r.returncode == 0, f"설치본 실행 종료 코드 0 (받은 값 {r.returncode})")
    check(state.exists(), "state.json 이 써짐")
    s = json.loads(state.read_text(encoding="utf-8"))
    check(s.get("exit") == "quit-after", "정해진 시간까지 살아 있다가 끝남")
    qml = [w for w in s.get("warnings", []) if ".qml" in w or "qrc:" in w]
    check(not qml, f"QML 경고 없음 {qml[:3]}")
    # 개발 배치와 같게 동작하는지: 모델 glb · 글꼴 · SVG 아이콘 · TLS 플러그인 · 기록 폴더(문서)
    d = s.get("deploy", {})
    check(d.get("installed") is True and same_path(d.get("dataDir"), INSTALL_DIR), f"설치본 배치로 인식(dataDir {d.get('dataDir')})")
    check(s.get("scene", {}).get("modelLoad") == "ok", f"로봇 모델 glb 를 읽음 ({s.get('scene', {}).get('modelLoad')})")
    fonts = d.get("fonts", [])
    check("Pretendard Variable" in fonts and "JetBrains Mono" in fonts, f"동봉 글꼴 로드 {fonts[:2]}")
    check(d.get("svg") is True and d.get("iconLoads") is True, "SVG 아이콘을 그림(imageformats/qsvg)")
    check(d.get("tls") is True and "schannel" in d.get("tlsBackends", []), f"TLS 백엔드 {d.get('tlsBackends')}")
    check(same_path(d.get("recordsPath"), documents() / "RobotDashboard" / "records"), f"기록 폴더는 문서 아래 ({d.get('recordsPath')})")


def same_path(a, b):
    return a is not None and os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(str(b)))


def documents():
    """문서 폴더(Qt 의 DocumentsLocation 과 같은 곳)."""
    p = reg_get(r"Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders", "Personal")
    return Path(os.path.expandvars(p)) if p else Path.home() / "Documents"


def main():
    setup = ROOT / "dist" / f"ROSMonitor-Setup-{version()}.exe"
    check(setup.exists(), f"설치 파일 {setup.name}")
    existing = reg_get(UNINSTALL_KEY, "InstallLocation")
    if existing and Path(existing.rstrip("\\")) != INSTALL_DIR:
        raise SystemExit(f"이 PC 에 ROS 모니터가 이미 설치되어 있습니다({existing}). 시험이 그 설치를 덮어쓰지 않게 멈춥니다.")
    uninstall()   # 지난 시험이 남긴 것
    reg_delete_tree(TEST_ROOT)
    shutil.rmtree(INSTALL_DIR, ignore_errors=True)

    tmp = tempfile.mkdtemp(prefix="rdinstall_")
    try:
        print("(a) 무인 설치")
        run_setup(setup, Path(tmp) / "install1.log")
        for rel in EXPECTED:
            check((INSTALL_DIR / rel).exists(), f"설치됨: {rel}")
        check(not list(INSTALL_DIR.glob("model/*.obj")), "원본 OBJ 는 넣지 않음")
        check(reg_get(TEST_KEY, "host") == "192.168.1.201", "처음 설치: host 기본값을 씀")
        check(reg_get(TEST_KEY, "port") == 9090, "처음 설치: port 기본값을 씀")
        check(reg_get(TEST_KEY, "updateCheck") == "true", "처음 설치: updateCheck 켬")
        check(reg_get(UNINSTALL_KEY, "DisplayVersion") == version(), "제거 항목의 버전")
        check(reg_get(UNINSTALL_KEY, "DisplayName") == "ROS Monitor", "제거 항목 이름 ROS Monitor")
        menu = Path(os.environ["APPDATA"]) / "Microsoft/Windows/Start Menu/Programs"
        check(any((menu / n).exists() for n in ("ROS 모니터.lnk", "ROS Monitor.lnk")), "시작 메뉴 바로 가기 'ROS 모니터'")
        smoke_run(tmp)

        print("(b) 업그레이드(같은 설치 파일로 다시)")
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, TEST_KEY) as k:
            winreg.SetValueEx(k, "host", 0, winreg.REG_SZ, "10.9.8.7")
            winreg.SetValueEx(k, "installerTestMarker", 0, winreg.REG_SZ, "keep")
        (INSTALL_DIR / "stale_marker.dll").write_text("old")
        (INSTALL_DIR / "model" / "old_part.glb").write_text("old")
        run_setup(setup, Path(tmp) / "install2.log")
        check(not (INSTALL_DIR / "stale_marker.dll").exists(), "이전 설치의 표식 파일이 지워짐")
        check(not (INSTALL_DIR / "model" / "old_part.glb").exists(), "이전 설치의 model 표식 파일이 지워짐")
        check((INSTALL_DIR / "robot_dashboard.exe").exists() and (INSTALL_DIR / "model/body.glb").exists(), "새 파일이 깔림")
        check(reg_get(TEST_KEY, "host") == "10.9.8.7", "사용자가 바꾼 host 가 그대로")
        check(reg_get(TEST_KEY, "installerTestMarker") == "keep", "시험용 설정 값이 그대로")
        check(not (INSTALL_DIR / "unins001.exe").exists(), "제거 항목이 하나뿐(같은 AppId)")

        print("(c) 무인 제거")
        uninstall()
        check(not (INSTALL_DIR / "robot_dashboard.exe").exists(), "프로그램 파일이 지워짐")
        check(not (INSTALL_DIR / "model").exists(), "model 폴더가 지워짐")
        check(reg_get(UNINSTALL_KEY, "DisplayName") is None, "제거 항목이 사라짐")
        check(not any((menu / n).exists() for n in ("ROS 모니터.lnk", "ROS Monitor.lnk")), "시작 메뉴 바로 가기가 사라짐")
        check(reg_get(TEST_KEY, "host") == "10.9.8.7" and reg_get(TEST_KEY, "installerTestMarker") == "keep",
              "무인 제거는 설정을 남김")
        print("인스톨러 시험 통과")
    finally:
        uninstall()
        reg_delete_tree(TEST_ROOT)
        shutil.rmtree(INSTALL_DIR, ignore_errors=True)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
