# 결정 기록 — ROS 모니터 MCP 서버 · 스킬

## 1. `mcp>=1.2` 가 mcp 2.x 를 받아 `mcp.server.fastmcp` 가 없음
- options: (a) `mcp>=1.2,<2` 로 고정 (b) FastMCP 를 먼저 쓰고 없으면 2.x 의 `MCPServer` 로 (c) 2.x 전용으로
- chosen: (b)
- why: 요구서의 실행 명령(`--with "mcp>=1.2"`)을 그대로 지키면서 1.x 에서도 돈다. 2.x 의 `MCPServer` 는 생성자 · `tool()` · `run("stdio")` 가 같다.
- effect: 1.x · 2.x 둘 다 `checks\mcp.cmd` 의 시험이 통과(1.x 는 `--with "mcp>=1.2,<2"` 로 확인). 고정하고 싶으면 register.py `server_command` 와 checks\mcp.cmd 의 `mcp>=1.2` 를 바꾼다.

## 2. `send_nav_goal(wait=true)` · `straight` 의 시간 초과 때 목표를 둘지 취소할지
- options: (a) 그대로 두고 'timeout' 만 알림 (b) 취소하고 멈춤
- chosen: (b) 시간이 다 되면 0 속도 ×3 · 직진 취소 · Nav2 취소(`stop_all`)
- why: 에이전트가 결과를 모르는 채로 로봇이 계속 달리는 상태를 남기지 않는다(안전 우선).
- effect: 오래 가는 목표는 `timeout_s` 를 늘리거나 `wait=false` 후 `nav_status` 로 본다. 바꾸려면 `send_nav_goal` 의 `res is None` 분기.

## 3. 어떤 도구에 `--allow-motion` 이 필요한가
- options: (a) 쓰기 도구 전부 (b) 멈추는 도구(`estop` · `cancel_nav`)는 빼고 전부
- chosen: (b) — `drive` · `straight` · `send_nav_goal` · `set_ramp` · `reset_map` · `set_initial_pose` 는 허용이 있어야 하고, `estop` · `cancel_nav` 는 언제나(단 `--no-publish` 면 거부)
- why: 멈추는 명령을 막으면 오히려 위험하다. 가감속 · 지도 · 초기 위치는 움직이지는 않지만 주행 동작을 바꾼다.
- effect: `motion_refused()` / `guarded()` 를 쓰는 도구 목록으로 바꾼다.

## 4. 목표 · 초기 위치의 프레임과 map 위치 출처
- options: (a) 대시보드처럼 odom 입력을 map→odom 으로 바꿔 보냄 (b) 도구 입력을 map 프레임으로 받아 그대로 보냄
- chosen: (b) — `/goal_pose` · `/dashboard/initialpose` 는 frame `map`. map 위치는 map→odom(`/map_odom` · `/tf`, 10 s 안) ∘ `/odom`, 없으면 `/amcl_pose`.
- why: 에이전트는 지도(`get_map_summary`) 좌표로 생각한다. 요구서도 "Nav2 목표 /goal_pose(PoseStamped, frame map)".
- effect: `get_pose.map` 과 같은 좌표계라 그대로 넣으면 된다.

## 5. `/nav/status` 가 없을 때 도착 추정 기준
- options: 거리 임계 · 정지 판정 · 진전 없음 시간
- chosen: map 위치가 목표에서 0.15 m 안이고 멈춰 있으면(|v| < 0.01, |w| < 0.02) `succeeded(추정)`, 20 s 동안 0.02 m 이상 가까워지지 않으면 `failed(추정)`. 결과에 "결과 토픽 /nav/status 없음 — 위치 변화로 …" 를 붙인다.
- why: Nav2 xy_goal_tolerance(대개 0.1~0.25 m)와 비슷하고, 제자리 회전 중(위치 그대로)도 20 s 는 기다린다.
- effect: `wait_nav()` 의 상수. `/nav/status` 가 생기면(다른 작업 병합 뒤) 그 끝 상태가 우선한다(목표를 보낸 뒤 받은 메시지만).

## 6. Codex(ChatGPT CLI) 스킬 위치
- options: `.codex/skills` · `~/.codex/skills` · 저장소 `.agents/skills`
- chosen: `.agents/skills/ros-monitor/SKILL.md`
- why: 이 저장소는 이미 Codex 용 하네스 스킬을 `.agents/skills/harness/SKILL.md` 에 두고, Codex 문서도 저장소의 `.agents/skills` 를 읽는다.
- effect: 다른 곳이 필요하면 파일을 복사만 하면 된다(내용은 도구 이름 · 절차뿐).

## 7. `.claude/skills/ros-monitor/SKILL.md` 를 쓰지 못함(이 세션의 권한 확인에 막힘)
- options: (a) 셸로 우회해 쓰기 (b) 쓰지 않고 남김
- chosen: (b) — 이 세션은 승인할 사람이 없어 `.claude/` 아래 쓰기가 자동 거부됐다. 우회하지 않는다.
- why: 작업 규칙(막힌 것을 우회하지 않는다).
- effect: **해야 할 일**: `.agents/skills/ros-monitor/SKILL.md` 를 그대로 `.claude/skills/ros-monitor/SKILL.md` 로 복사한다(내용이 같아야 한다 — Claude Code · Codex 모두 같은 YAML 머리말 `name` · `description` 형식).
  `copy .agents\skills\ros-monitor\SKILL.md .claude\skills\ros-monitor\SKILL.md` (폴더가 없으면 `mkdir .claude\skills\ros-monitor`). README 는 두 경로를 모두 적어 두었다.

## 8. Codex config 의 시간 상한
- options: 기본값(시작 10 s · 도구 60 s) 그대로 / 늘림
- chosen: `startup_timeout_sec = 120` · `tool_timeout_sec = 300`
- why: 처음 실행은 uv 가 패키지를 받느라 10 s 를 넘고, `send_nav_goal` 은 기본 120 s 까지 기다린다.
- effect: register.py `codex_section()`.
