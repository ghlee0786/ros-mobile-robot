"""ROS 모니터 릴리스 만들기(표준 라이브러리만).

    uv run --no-project python tooling/release/release.py 0.2.0             # 확인 + 설치 파일 빌드, 올릴 내용만 보여 줌(dry run)
    uv run --no-project python tooling/release/release.py 0.2.0 --publish   # + git tag v0.2.0 · GitHub 릴리스 · 설치 파일 올리기

하는 일
1. dashboard/CMakeLists.txt 의 project(VERSION) 이 0.2.0 인지, CHANGELOG.md 에 '## [0.2.0]' 항목이 있는지 확인
2. installer\\build_installer.cmd 로 dist\\ROSMonitor-Setup-0.2.0.exe 를 만든다
3. --publish 일 때만: 로컬에 태그 v0.2.0 을 만들고 push 명령을 알려 준 뒤, GitHub API 로 릴리스(본문 = CHANGELOG 의 그 항목)를
   만들고 설치 파일을 asset 으로 올린다. 토큰은 `git credential fill`(Git Credential Manager)로 얻고 화면 · 파일에 남기지 않는다.
   ROS 모니터(대시보드)는 이 릴리스의 tag_name 과 ROSMonitor-Setup-*.exe(옛 이름 RobotDashboard-Setup-*.exe 도)를 보고 새 버전을 알린다(dashboard/src/update).
"""
import argparse
import json
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPO = "ghlee0786/ros-mobile-robot"
API = f"https://api.github.com/repos/{REPO}"


def fail(msg):
    sys.exit("릴리스 중단: " + msg)


def git(*args, check=True):
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    if check and r.returncode != 0:
        fail(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout.strip()


def changelog_entry(version):
    text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    m = re.search(rf"^## \[{re.escape(version)}\][^\n]*\n(.*?)(?=^## \[|\Z)", text, re.M | re.S)
    if not m or not m.group(1).strip():
        fail(f"CHANGELOG.md 에 '## [{version}] - YYYY-MM-DD' 항목(내용 포함)이 없습니다")
    return m.group(1).strip()


def token():
    r = subprocess.run(["git", "credential", "fill"], input="protocol=https\nhost=github.com\n\n",
                       capture_output=True, text=True, cwd=ROOT)
    for line in r.stdout.splitlines():
        if line.startswith("password="):
            return line.split("=", 1)[1]
    fail("git credential fill 로 GitHub 토큰을 얻지 못했습니다(Git Credential Manager 로 한 번 로그인하세요)")


def api(method, url, tok, data=None, content_type="application/json"):
    req = urllib.request.Request(url, data=data, method=method, headers={
        "Authorization": f"Bearer {tok}", "Accept": "application/vnd.github+json",
        "User-Agent": "robot-dashboard-release", "Content-Type": content_type})
    try:
        with urllib.request.urlopen(req, timeout=600) as r:
            return json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        fail(f"{method} {url.split('?')[0]} → {e.code} {e.read().decode('utf-8', 'replace')[:300]}")


def main():
    ap = argparse.ArgumentParser(description="ROS 모니터 릴리스(설치 파일 빌드 · GitHub Releases 올리기)")
    ap.add_argument("version", help="x.y.z (앞의 v 없이)")
    ap.add_argument("--publish", action="store_true", help="태그를 만들고 GitHub 릴리스를 올린다(없으면 dry run)")
    a = ap.parse_args()
    ver = a.version.lstrip("v")
    if not re.fullmatch(r"\d+\.\d+\.\d+", ver):
        fail("버전은 x.y.z 형식이어야 합니다")
    tag = "v" + ver

    m = re.search(r"project\(robot_dashboard VERSION (\S+)", (ROOT / "dashboard/CMakeLists.txt").read_text(encoding="utf-8"))
    if not m or m.group(1) != ver:
        fail(f"dashboard/CMakeLists.txt 의 VERSION 이 {m.group(1) if m else '없음'} 입니다 — {ver} 로 고친 뒤 커밋하세요")
    body = changelog_entry(ver)
    print(f"[확인] CMakeLists VERSION {ver} · CHANGELOG 항목 {len(body.splitlines())} 줄")

    print("[빌드] installer\\build_installer.cmd")
    if subprocess.run(["cmd", "/c", str(ROOT / "installer" / "build_installer.cmd")], cwd=ROOT).returncode != 0:
        fail("설치 파일 빌드 실패")
    # 지금 이름 ROSMonitor-Setup-*, 없으면 옛 이름 RobotDashboard-Setup-*
    found = [p for p in (ROOT / "dist" / f"{n}-Setup-{ver}.exe" for n in ("ROSMonitor", "RobotDashboard")) if p.exists()]
    if not found:
        fail(f"dist\\ROSMonitor-Setup-{ver}.exe 가 없습니다")
    setup = found[0]
    print(f"[빌드] {setup.relative_to(ROOT)} ({setup.stat().st_size / 1e6:.1f} MB)")

    head = git("rev-parse", "HEAD")
    plan = [f"git tag -a {tag} -m \"{tag}\" (커밋 {head[:10]})",
            f"GitHub 릴리스 {tag} 만들기 — {API}/releases (본문 = CHANGELOG [{ver}])",
            f"asset 올리기 — {setup.name}"]
    if not a.publish:
        print("\n[dry run] --publish 를 주면 다음을 합니다:")
        for p in plan:
            print("  - " + p)
        print("\n--- 릴리스 본문 ---\n" + body)
        return

    if git("status", "--porcelain"):
        fail("커밋하지 않은 변경이 있습니다 — 릴리스는 커밋된 상태에서 만듭니다")
    if not git("tag", "--list", tag):
        git("tag", "-a", tag, "-m", tag)
        print(f"[태그] {tag} 를 만들었습니다. 원격에 올리려면:  git push origin HEAD {tag}")
    tok = token()
    rel = api("POST", f"{API}/releases", tok, json.dumps({
        "tag_name": tag, "target_commitish": head, "name": tag, "body": body,
        "draft": False, "prerelease": False}).encode("utf-8"))
    upload = rel["upload_url"].split("{")[0] + f"?name={setup.name}"
    asset = api("POST", upload, tok, setup.read_bytes(), "application/octet-stream")
    print(f"[릴리스] {rel['html_url']}\n[asset] {asset.get('name')} ({asset.get('size')} 바이트)")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
