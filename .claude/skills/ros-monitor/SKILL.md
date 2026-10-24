---
name: ros-monitor
description: ROS 모니터(라즈베리 파이 이동 로봇) 조종 · 상태 확인 — "로봇 앞으로 가", "(x, y) 로 이동", "주변 장애물", "지도 보여 줘", "멈춰", "로봇 상태" 같은 요청에 ros-monitor MCP 도구(robot_status · get_pose · get_scan_summary · get_map_summary · nav_status · send_nav_goal · drive · straight · estop …)로 답하고 수행한다.
---

# ROS 모니터 — 로봇을 읽고 조종하기

ros-monitor MCP 서버(`tooling/mcp/ros_monitor_mcp.py`)가 파이 rosbridge 에 대시보드와 같은 규칙으로 붙는다.
도구가 보이지 않으면 `python tooling/mcp/register.py --dry-run` 으로 등록 내용을 보여 주고, 사용자가 승인하면
`python tooling/mcp/register.py`(움직임까지 허용하려면 `--allow-motion`)로 등록한 뒤 앱을 다시 시작하라고 알린다.

## 도구

| 읽기 | 하는 일 |
|---|---|
| `robot_status` | 연결 · 토픽 주기 · 워치독/가감속 · 직진 · Nav2 상태 · 배터리 · 움직임 허용 여부 |
| `get_pose` | odom · map 위치(m)와 방향(도), 속도 |
| `get_scan_summary` | 8 방위 최근접 거리(앞 0°, 왼쪽 +90°) |
| `get_map_summary(radius_m)` | 지도 크기 · 해상도 · 알려진 비율 · 로봇 주변 ASCII(`#` 벽 `.` 빈 곳 `R` 로봇) |
| `nav_status` · `list_topics` | `/nav/status` 최신 / 토픽 목록 |

| 쓰기(로봇이 움직인다) | 하는 일 |
|---|---|
| `send_nav_goal(x, y, yaw_deg, wait, timeout_s)` | Nav2 목표(map 프레임). 기다리면 끝 상태 · 한국어 이유 |
| `drive(linear, angular, duration_s)` | 10 Hz 속도 명령(워치독 속도 한계 안 · ≤ 5 s), 끝나면 0 |
| `straight(distance_m)` | IMU 직진(\|d\| ≤ 2 m, 음수 = 후진) |
| `cancel_nav` · `estop` | 목표 취소 + 0 속도 / 비상 정지 — 허용 없이도 언제나 된다 |
| `set_ramp(enabled)` · `reset_map` · `set_initial_pose(x, y, yaw_deg)` | 가감속 / 지도 초기화 / AMCL 초기 위치 |
| `get_speed_limits` · `set_speed_limits(max_forward, max_reverse, max_angular)` | 워치독 속도 한계(모든 속도 명령에 적용, 파이에 저장) 읽기 / 바꾸기 — 바꾸기 전에 값을 사람에게 말하고 확인받는다 |

움직이는 도구는 서버가 `--allow-motion` 으로 시작했을 때만 된다. 거부되면 그 이유(`refused`)를 그대로 전하고, 몰래 우회하지 않는다.

## 안전 절차 — 움직이기 전에 반드시

1. **상태 확인**: `robot_status`(connected · watchdog.present · odom 주기) → `get_pose` → `get_scan_summary`.
   연결이 없거나 워치독이 없으면 움직이지 않고 알린다.
2. **사람에게 알림**: 무엇을 · 어디로 · 얼마나 움직일지 한 줄로 말한다(예: "앞 0.95 m 에 벽 — 0.3 m 만 전진합니다").
   가는 방향의 최근접 거리가 이동 거리 + 0.3 m 보다 짧으면 하지 않는다.
3. **움직임**: 짧게 나눠서 — `drive` 는 한 번에 몇 초, 먼 곳은 `send_nav_goal`.
4. **확인**: `get_pose` · `nav_status` 로 결과를 보고 말한다. 이상하면 바로 `estop`.

사용자가 "멈춰" · "stop" 이라고 하면 다른 것을 묻지 말고 먼저 `estop`.
도중에 연결이 끊기면 파이 cmd_watchdog 가 `/teleop/cmd_vel` 이 0.5 s 끊긴 뒤 로봇을 세운다. 다시 붙으면 서버가 정지를 다시 보낸다.

## 예

- "앞으로 30 cm" → 상태 확인 → `straight(distance_m=0.3)` 또는 `drive(linear=0.1, duration_s=3)` → `get_pose` 로 이동 거리 보고.
- "왼쪽으로 90도" → `drive(linear=0, angular=0.5, duration_s=3.1)` (각도 = w × 시간, 실제 회전은 `moved.turn_deg` 로 확인).
- "(1.5, 0.5) 로 가" → `get_map_summary` 로 목표가 빈 곳인지 → `send_nav_goal(x=1.5, y=0.5, yaw_deg=0)` → `state` · `reason` 보고.

## 실패 이유별 대응(`send_nav_goal` 의 `state` · `reason` · `error_msg`)

| 이유(키워드) | 대응 |
|---|---|
| 출발점이 막힘 · start occupied · 로봇 위치가 지도 밖 | 지도가 로봇을 벽 속에 그린 것 — 사람에게 알린 뒤 `reset_map`, 몇 초 뒤 `get_map_summary` 로 확인하고 다시 시도 |
| 목표가 막힘 · goal occupied · 지도 밖 | `get_map_summary` 로 가까운 빈 곳(`.`)을 골라 제안 |
| 경로 없음 · no valid path | 지도 미리보기로 막힌 통로를 설명하고 중간 목표로 나눠 보낸다 |
| 위치 추정 틀림(로봇이 엉뚱한 곳을 향함) | `estop` → 사람에게 실제 위치를 묻고 `set_initial_pose` |
| 제자리 · 진전 없음 · 복구 반복(recoveries 증가) | `get_scan_summary` 로 가까운 장애물 확인 → 짧게 `drive` 로 빼거나 사람에게 치워 달라고 |
| timeout | 서버가 이미 목표를 취소했다 — 남은 거리와 함께 알리고 다시 보낼지 묻는다 |
| `/nav/status 없음`(추정) | PC 내비 스택이 결과 토픽을 아직 내지 않는다 — 결과는 위치로 추정한 것이라고 밝힌다 |
| 연결 실패 · 끊김 | `robot_status` 로 주소 · 연결 확인. 파이 전원 · Wi-Fi(192.168.1.201) · rosbridge 를 사람에게 확인 요청 |
