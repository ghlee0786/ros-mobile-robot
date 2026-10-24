# PLAN — ROS 모니터 MCP 서버 · 등록 도구 · 스킬

## 목표
ROS 모니터(로봇 대시보드 제품)에 AI 에이전트용 MCP 서버를 만든다 — Claude Code · Claude Desktop · Codex CLI(ChatGPT)에서
로봇 상태를 읽고 조종할 수 있게. 등록 도구 · 스킬 · README 절 · 모의 서버 위 시험(`checks\mcp.cmd`).

## 진행
- [x] `tooling/mcp/ros_monitor_mcp.py` — FastMCP(mcp 1.x) / MCPServer(mcp 2.x) + websockets, stdio
  - 읽기: robot_status · get_pose · get_scan_summary · get_map_summary · nav_status · list_topics
  - 쓰기: send_nav_goal · cancel_nav · estop · drive · straight · set_ramp · reset_map · set_initial_pose
  - 안전: `--allow-motion`/`ROS_MONITOR_ALLOW_MOTION=1` 없으면 움직이는 도구 거부(estop · cancel_nav 는 언제나), `--no-publish` 읽기 전용,
    예외 · 끊김 · 취소 때 `stop_all`(0 ×3 · 직진 취소 · nav 취소), 움직이던 중 끊겼다 다시 붙으면 정지를 먼저 보냄
  - 주소: `--url` > `ROS_MONITOR_URL` > HKCU\Software\RobotDashboard\RobotDashboard host/port > ws://192.168.1.201:9090
- [x] `tooling/mcp/register.py` — Claude Code(`claude mcp remove`+`add --scope user`) · Claude Desktop(json 병합 · 백업) · Codex(config.toml 병합 · 백업), `--dry-run` · `--allow-motion` · `--url` · `--no-claude-code/--no-desktop/--no-codex`
- [x] `tooling/mcp/test_ros_monitor_mcp.py` + `checks/mcp.cmd`
- [x] Codex 스킬 `.agents/skills/ros-monitor/SKILL.md`
- [ ] Claude Code 스킬 `.claude/skills/ros-monitor/SKILL.md` — **쓰기가 권한 확인에 막힘**(DECISIONS 7). `.agents/skills/ros-monitor/SKILL.md` 를 그대로 복사하면 끝.
- [x] README "🧠 AI 에이전트로 조종하기(MCP)" 절(🤖 실제 로봇 뒤)

## 새로 온 사람을 위한 맥락
- 토픽 · 서비스 · 안전 규칙의 근거: `dashboard/src/robot.cpp`(sendTwist · sendStop · straight · sendGoal · setInitialPose · cancelGoal · resetMap · estop · setRampEnabled),
  `dashboard/src/msgparse.cpp` initialPoseCovariance, `CONTRIBUTING.md` 4절, 파이 노드 `tooling/rpi/robot_link/{cmd_watchdog_node,straight_drive_node}.py`.
- 모의 서버(`tooling/mockbridge`, 다른 작업 소유 — 고치지 않음): `--nav` 는 /amcl_pose · /navigate_to_pose/_action/status 를 내고 `/goal_pose` 를 직선으로 따라간다.
  map→odom 은 `--map-odom` 이 없으면 없음(→ map 위치 = /amcl_pose). 물리 상한 0.1 m/s · 0.25 rad/s. `/nav/status` · `/dashboard/nav_cancel` · `/teleop/estop` 은 처리하지 않는다
  (Nav2 취소는 `stop_all` 이 함께 부르는 `/navigate_to_pose/_action/cancel_goal` 서비스로 모의 서버에서도 멈춘다). `ramp_enabled` 매개변수도 없다(set_ramp 는 "매개변수 없음").
- `/nav/status`(std_msgs/String JSON) 는 다른 작업(tooling/nav · dashboard)이 만든다. 서버는 목표를 보낸 뒤 받은 `/nav/status` 의 끝 상태를 쓰고,
  없으면 위치로 추정한다(DECISIONS 5).
- mcp SDK 는 `mcp>=1.2` 로 2.x(2.3.0)가 깔린다 — `FastMCP` 가 `MCPServer` 로 바뀌어 import 를 둘 다 시도한다. 결과 객체도 `isError` → `is_error`.
- 시험은 각 테스트마다 MCP 서버를 `sys.executable`(uv 환경)로 stdio 로 띄운다. 모의 서버는 모듈 fixture 로 한 번(`--port 0 --nav`).
  등록 시험은 HOME · USERPROFILE · APPDATA 를 tmp 로 바꾸고 Claude Code 는 `--dry-run` 출력만 본다(사용자 설정을 바꾸지 않음).
- 등록은 **메인 체크아웃(E:\workspace\cpp\Ros)에서** 실행해야 한다 — register.py 는 자기 위치의 ros_monitor_mcp.py 절대 경로를 넣는다(worktree 에서 돌리면 worktree 경로가 들어간다).
  실제 등록(사용자 설정 변경)은 대화 세션이 사용자 승인 뒤 한다.

## 검증(이 세션에서 실행)
- `cmd /c checks\mcp.cmd` → `5 passed in ~30 s`, EXIT=0 (연속 3 회 + 마지막 수정 뒤 1 회). mcp 2.3.0.
- `uv run --no-project --with "mcp>=1.2,<2" --with "websockets>=12,<14" --with pytest python -m pytest tooling\mcp -q` → `5 passed` (mcp 1.x FastMCP 경로).
- `python tooling\mcp\register.py --dry-run` (실제 사용자 설정을 읽기만) → 세 곳 모두 "추가" 로 표시, 아무것도 바꾸지 않음, EXIT=0.
- 시험이 확인하는 것: 도구 14 개 목록 · robot_status(연결 · 워치독 · odom 주기 > 5 Hz · nav 없음) · get_pose(odom/map) · get_scan_summary(/scan_compact, 8 방위) ·
  get_map_summary(R 표시) · list_topics · nav_status 없음 · `--allow-motion` 없을 때 6 개 거부 + 위치 그대로 · `--no-publish` 거부 ·
  drive(0.1, 0, 1 s) 10±1 명령 · 0.05 m 넘게 이동 · 끝나고 정지 · 속도 자르기(0.25 / -0.6) · send_nav_goal 0.3 m 앞 → succeeded(추정) · 0.15 m 안 ·
  2 m 목표 달리는 중 estop → 정지 · straight 0.2 m reached · 3 m 거부 · register 병합(기존 항목 · 백업 · 갱신 시 중복 없음 · tomllib 로 파싱).
- 처음 한 번 `test_tools_and_reads` 가 `/odom` 주기 0 으로 실패 — 첫 호출 때 /odom 이 하나뿐이라 주기를 못 잼. `ready()` 가 /odom 5 개(최대 2 s)를 기다리게 고침.
  (그때 실행이 2 분 걸린 것은 기계 부하로 보인다 — 다시 재면 같은 시험 3.8 s.)

## 남은 일
- `.claude/skills/ros-monitor/SKILL.md` 복사(위).
- 실제 로봇 시험은 하지 않았다(규칙: 파이에 접속하지 않음). 실기에서 `/nav/status` 가 생긴 뒤 send_nav_goal 이 그 끝 상태를 돌려주는지 확인할 것.
