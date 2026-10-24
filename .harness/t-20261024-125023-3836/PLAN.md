# PLAN — Nav2 결과 · 실패 이유를 대시보드와 MCP 가 알게 (/nav/status)

## 목표
대시보드에서 Nav2 목표를 찍으면 "보냄" 만 보이고, Nav2 가 경로 계획에 실패해도(출발점이 팽창 영역 안) 화면에 아무것도 없었다.
PC 에 NavigateToPose 액션 클라이언트(nav_goal_bridge)를 두어 결과 · 피드백 · 실패 이유를 `/nav/status`(JSON)로 내고,
대시보드 · 모의 서버 · 시험이 그것을 쓰게 한다. MCP(tooling/mcp/, 다른 작업)는 같은 계약을 쓴다.

## /nav/status 계약 (바꾸지 말 것 — MCP 작업과 공유)
`{"seq", "state": idle|planning|running|succeeded|failed|canceled, "goal": {"x","y","yaw"}|null, "distance_remaining", "eta_s",
"recoveries", "error_code", "error_msg", "reason", "stamp"}` — 진행 중 2 Hz, 바뀌면 즉시, 끝난 뒤 5 s 마다 다시.
goal 은 받은 PoseStamped 의 프레임 그대로(대시보드는 map→odom 이 신선하면 map, 아니면 odom).

## 진행
- [x] `tooling/nav/nav_goal_bridge.py` — /dashboard/goal_pose → /navigate_to_pose 액션, /nav/status 발행. 순수 함수
      `cell_cost` · `fail_reason` · `state_of` · `make_status` 는 ROS import 없이 모듈 위쪽에(ROS import 는 main() 안).
      실패 시 /global_costmap/costmap(transient_local) 에서 출발점(base_link) · 목표 칸 값을 TF 로 바꿔 읽어 ≥ 99 = 막힘, -1 = 미지.
      새 목표가 오면 seq+1, 이전 목표의 피드백 · 결과는 버린다(Nav2 가 선점).
- [x] `tooling/nav/test_nav_goal_bridge.py` — unittest 15 개(칸 인덱스 · 회전 지도 · 이유 우선순위 · 계약 키).
- [x] `tooling/rpi/robot_link/nav_relay_topics.sh` — 로봇 /goal_pose → PC /dashboard/goal_pose(/dashboard/initialpose 와 같은 방식, 목록 맨 뒤),
      /nav/status 를 NAV_RELAY_TO_ROBOT 맨 뒤에. `run_bridge.sh` 에서 nav_goal_bridge 기동, `run_nav.sh` pkill 목록에 추가.
- [x] 대시보드: `msgparse::parseNavStatus` · `navStatusMatches` · `navStatusEvent`(순수), `Robot::navStatus` 속성,
      sendGoal 성공 뒤 /nav/status 를 throttle 500 ms 로 구독(reason "nav"), 자율주행에서 벗어나면 해제(진행 중이면 '취소됨').
      5 s 동안 상태가 없으면 nav_goal_bridge 확인 안내. 이벤트 로그: 계획 중 · 이동 시작 · 도착 · 실패 — 이유 · 취소됨(2 Hz 갱신은 남기지 않음).
- [x] `qml/SceneView.qml` — 오른쪽 아래 목표 HUD 의 "· 보냄" 을 navStatus.text 로(실패 빨강 · 도착 초록), 실패면 위쪽 가운데에
      Banner(원인) + Btn `지도 초기화`(danger) + 아래 줄 할 일. scene.navState · scene.goalShown 속성.
- [x] 모의 서버: `--nav` 가 /nav/status(std_msgs/String) 를 계약대로 낸다(도착은 기존 경로 추종으로), `--nav-fail plan` 이면 0.8 s 뒤
      failed · error_code 208 · reason = nav_goal_bridge.START_BLOCKED 와 같은 글. test_sim.py 에 성공 · 실패 시험 2 개.
- [x] 시나리오 `nav_ok.json`(도착 · throttle 500 확인) · `nav_fail.json`(실패 · reason · 배너 상태 · /plan 없음).
- [x] `dashboard/tests/tst_nav_status.cpp` — 파싱 · 일치 판정 · 이벤트 전환 · Robot 초기 상태.
- [x] `dashboard/README.md` 설명 추가.

## 검증 (이 세션에서 실행)
- `python -m unittest tooling/nav/test_nav_goal_bridge.py` → Ran 15 tests, OK
- `uv run ... python -m pytest tooling/mockbridge/tests/test_sim.py -q` → 15 passed
- `dashboard\test.cmd` → 100% tests passed, 0 failed out of 21 (tst_nav_status 포함)
- `run_scenarios.py --offline --only nav_ok,nav_fail,goal,safety_nav_conflict,estop` → 합격 5 · 불합격 0
  (nav_fail 캡처로 배너가 레이어 패널 · 뷰 큐브와 겹치지 않는 것 확인 — 처음 한 줄 배너는 겹쳐서 두 줄로 바꿈)
- `checks\integration.cmd` → 단위 시험 21/21 통과 · smoke 통과 · mockbridge pytest 64 passed · 시나리오 합격 51 · 불합격 1 (cmd 종료 코드 0)
  - 불합격 `perf_odom25` 는 이번 변경과 무관한 기존 문제다: 시나리오가 `robot.armTeleop` 없이 `robot.straight(1.0)` 을 불러
    대시보드가 "직진 거부 — 조종이 잠겨 있음" 으로 끝난다(state.json events). 단독으로 두 번 돌려도 같다. 이 시나리오 · 조종 잠금 코드는 건드리지 않았다.

## 남은 일 (이 작업 밖)
- 실기 배포: 파이와 PC 의 nav_relay_topics.sh 를 함께 바꾸고(deploy_robot_link.sh) robot-nav-relay.service · run_nav.sh 재기동 후
  실제 Nav2 로 성공 · 출발점 막힘 실패를 확인(대화 세션 몫). nav_goal_bridge 는 ROS 실기에서 아직 돌려 보지 않았다.
- 루트 CHANGELOG.md 항목(쓰기 범위 밖) — DECISIONS.md 5.

## 새로 온 사람을 위한 위치
- PC 노드: tooling/nav/nav_goal_bridge.py (머리말에 계약), 기동 tooling/nav/run_bridge.sh
- 중계 목록: tooling/rpi/robot_link/nav_relay_topics.sh (양쪽 같은 순서 — 인덱스로 이어진다)
- 대시보드: dashboard/src/robot.cpp (onNavStatus · setNavStatus · stopNavStatus · sendGoal), msgparse.cpp 끝, qml/SceneView.qml (navFailBanner · goalHudText)
- 모의: tooling/mockbridge/mockbridge/sim.py (nav_set · nav_tick · make_nav_status), 시나리오 nav_ok · nav_fail
