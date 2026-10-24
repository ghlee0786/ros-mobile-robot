"""ROS 모니터 MCP 서버를 Claude Code · Claude Desktop · Codex CLI(ChatGPT 의 CLI)에 등록한다(표준 라이브러리만).

    python tooling/mcp/register.py --dry-run            무엇을 바꿀지만 보여 준다
    python tooling/mcp/register.py                      셋 다 등록(이미 있으면 갱신) — 읽기 · 멈춤 도구만 켜진다
    python tooling/mcp/register.py --allow-motion       움직이는 도구도 켠다(사람이 로봇 곁에 있을 때만)
    python tooling/mcp/register.py --no-claude-code --url ws://127.0.0.1:9091

Claude Code 는 `claude mcp add ros-monitor --scope user -- …` 를 실행하고, Claude Desktop 은
%APPDATA%\\Claude\\claude_desktop_config.json 의 mcpServers, Codex 는 ~/.codex/config.toml 의 [mcp_servers.ros-monitor] 에
병합한다(다른 항목은 그대로, 바꾸기 전 파일은 <이름>.bak-<시각> 으로 남긴다).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

NAME = "ros-monitor"
SERVER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ros_monitor_mcp.py")


def server_command(args):
    # Claude Desktop 은 사용자 PATH 를 다 물려받지 않을 수 있어 uv 를 절대 경로로 쓴다
    uv = shutil.which("uv") or "uv"
    cmd = [uv, "run", "--no-project", "--with", "mcp>=1.2", "--with", "websockets>=12,<14", "python", SERVER]
    if args.allow_motion:
        cmd.append("--allow-motion")
    if args.url:
        cmd += ["--url", args.url]
    env = {"UV_CACHE_DIR": args.uv_cache}   # C: 드라이브에 캐시를 두지 않는다
    return cmd, env


def backup(path, dry):
    if os.path.exists(path) and not dry:
        shutil.copy2(path, f"{path}.bak-{time.strftime('%Y%m%d-%H%M%S')}")


def claude_code(cmd, env, dry):
    claude = shutil.which("claude")
    add = [claude or "claude", "mcp", "add", NAME, "--scope", "user"]
    for k, v in env.items():
        add += ["-e", f"{k}={v}"]
    add += ["--", *cmd]
    if dry:
        # 그대로 cmd 에 붙여 넣어도 '>' · '<' 가 리디렉션이 되지 않게 따옴표를 친다(실제 실행은 셸을 거치지 않는다)
        shown = " ".join(f'"{a}"' if re.search(r'[\s<>|&^]', a) else a for a in add)
        print(f"[Claude Code] (있으면 지우고) 실행: {shown}")
        return True
    if not claude:
        print("[Claude Code] claude 명령이 없어 건너뜀")
        return False
    # 갱신은 지우고 다시 넣는다(claude mcp 에는 update 가 없다). 없을 때의 remove 실패는 무시한다
    subprocess.run([claude, "mcp", "remove", NAME, "--scope", "user"], capture_output=True)
    r = subprocess.run(add, capture_output=True, text=True)
    print(f"[Claude Code] {'등록함' if r.returncode == 0 else '실패: ' + (r.stderr or r.stdout).strip()}")
    return r.returncode == 0


def claude_desktop(cmd, env, dry):
    appdata = os.environ.get("APPDATA") or os.path.expanduser("~/AppData/Roaming")
    path = os.path.join(appdata, "Claude", "claude_desktop_config.json")
    cfg = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        cfg = json.loads(text) if text.strip() else {}
    entry = {"command": cmd[0], "args": cmd[1:], "env": env}
    old = cfg.get("mcpServers", {}).get(NAME)
    print(f"[Claude Desktop] {path}: mcpServers.{NAME} {'갱신' if old else '추가'}"
          + (" (같음)" if old == entry else "") + f"\n  {json.dumps(entry, ensure_ascii=False)}")
    if dry or old == entry:
        return True
    backup(path, dry)
    cfg.setdefault("mcpServers", {})[NAME] = entry
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)
        f.write("\n")
    return True


def toml_str(s):
    return json.dumps(s, ensure_ascii=False)   # JSON 문자열은 TOML 기본 문자열로도 맞다


def codex_section(cmd, env):
    return "\n".join([
        f"[mcp_servers.{NAME}]",
        f"command = {toml_str(cmd[0])}",
        f"args = [{', '.join(toml_str(a) for a in cmd[1:])}]",
        f"env = {{ {', '.join(f'{k} = {toml_str(v)}' for k, v in env.items())} }}",
        # 처음 실행은 uv 가 패키지를 받느라 기본 10 s 를 넘을 수 있고, send_nav_goal 은 기본 도구 시간(60 s)보다 오래 기다린다
        "startup_timeout_sec = 120",
        "tool_timeout_sec = 300",
    ]) + "\n"


def strip_codex_section(text):
    """[mcp_servers.ros-monitor] 과 그 하위 표([mcp_servers.ros-monitor.env] 등)를 뺀다."""
    head = re.compile(r'^\s*\[\s*mcp_servers\s*\.\s*("?)' + re.escape(NAME) + r'\1\s*(\.[^\]]*)?\]\s*$')
    out, skip = [], False
    for line in text.splitlines(keepends=True):
        if line.lstrip().startswith("["):
            skip = bool(head.match(line))
        if not skip:
            out.append(line)
    return "".join(out)


def codex(cmd, env, dry):
    home = os.environ.get("CODEX_HOME") or os.path.join(os.path.expanduser("~"), ".codex")
    path = os.path.join(home, "config.toml")
    text = ""
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            text = f.read()
    rest = strip_codex_section(text).rstrip()
    section = codex_section(cmd, env)
    new = (rest + "\n\n" if rest else "") + section
    print(f"[Codex CLI] {path}: [mcp_servers.{NAME}] {'갱신' if rest != text.rstrip() else '추가'}\n"
          + "".join("  " + ln + "\n" for ln in section.splitlines()))
    if dry or new == text:
        return True
    backup(path, dry)
    os.makedirs(home, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(new)
    return True


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dry-run", action="store_true", help="바꿀 내용만 보여 준다")
    p.add_argument("--allow-motion", action="store_true", help="움직이는 도구를 켠 채로 등록한다")
    p.add_argument("--url", help="rosbridge 주소를 고정한다(없으면 서버가 대시보드 설정을 읽는다)")
    p.add_argument("--uv-cache", default=os.environ.get("UV_CACHE_DIR") or r"E:\Temp\uv-cache", help="UV_CACHE_DIR")
    p.add_argument("--no-claude-code", action="store_true")
    p.add_argument("--no-desktop", action="store_true")
    p.add_argument("--no-codex", action="store_true")
    args = p.parse_args(argv)
    cmd, env = server_command(args)
    ok = True
    if not args.no_claude_code:
        ok &= claude_code(cmd, env, args.dry_run)
    if not args.no_desktop:
        ok &= claude_desktop(cmd, env, args.dry_run)
    if not args.no_codex:
        ok &= codex(cmd, env, args.dry_run)
    if args.dry_run:
        print("(--dry-run: 아무것도 바꾸지 않음)")
    else:
        print("등록한 앱을 다시 시작하면 ros-monitor 도구가 보인다.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
