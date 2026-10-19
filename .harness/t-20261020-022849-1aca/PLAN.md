# P6 대시보드 화면 — 계획 · 진행

## 목표
DashboardPage 의 바퀴 · IMU 그래프를 PlotFeeder 기반 TimeGraph 로 바꾸고, 링크 줌 · 사용자 그래프 · 가상 조이스틱 · 조종 잠금(Arm)을 더한다. 검사: `checks\p4_core.cmd`.

## 진행
- [x] Robot: `teleopArmed`(Q_PROPERTY) · `armTeleop(bool)` · 시험용 `armIdleMs`(기본 30000) · `recordsDir()`. 연결/끊김 · linkLost · estop · 무입력이면 잠금. 잠긴 동안 drive() 입력 버림, 0 은 10 Hz 유지.
- [x] tests/tst_arm.cpp (잠김 0 송신 · 해제 송신 10 Hz · estop · 무입력 · 재연결)
- [x] src/plot: PlotSeries `steps` 속성 + PlotFeeder::visiblePoints 가 decimate(steps) 를 쓴다(목표 계열 계단).
- [x] qml/dash/TimeGraph.qml · GraphLink.qml · UserGraphPanel.qml · Joystick.qml · ArmControl.qml, cmake/dashui.cmake
- [x] DashboardPage: 바퀴(sampled push, 목표 계단) · IMU TimeGraph, Metric(stale), 사용자 그래프 패널, 조이스틱 · 잠금 UI, 잠김 안내, Ctrl+Enter 해제, 입력칸 포커스 중 조종 키 무시. objectName dash · wheelGraph · imuGraph · userGraph · joystick · dashScroll.
- [x] StatusExtraDash.qml 에 조종 칩
- [x] 확인: checks\p4_core.cmd 통과, .harness/<id>/dash_check.py(가짜 rosbridge) 기본 · --locked · --joy 통과

## 발견
- PlotFeeder 의 자동 Y 는 계열 값이 하나뿐(상수)이면 TimeBuffer::yRange 가 ±1 띠를 준다(P5 설계) — 상수 신호는 Y 가 넓게 보인다.
- 기계가 바쁘면 앱 기동이 8 s 까지 늘어 dash_check 의 cmd_vel 개수가 줄 수 있다(코드 문제 아님).
- exportCsv 는 push 계열의 topic · path 칸을 비운다(계열 이름 칸 없음, P5 형식).

## 남은 일(통합 단계)
- 모의 서버(P1) 병합 뒤 시나리오: dash_graphs.json(normal + armTeleop(true) · drive(1,0) 5 s · win.page 0 · `set wheelGraph.feeder.paused true` · shot → state robot.teleopArmed true, /teleop/cmd_vel ≤ 0.10, 금지어 없음), dash_arm.json(잠근 채 drive(1,0) → cmd_vel 전부 0, `set robot.armIdleMs 3000` 뒤 무입력 자동 잠금). 기존 normal · estop · flood 의 actions 에 `wait bridge.connected` 다음 `call robot.armTeleop [true]` 줄 추가.
