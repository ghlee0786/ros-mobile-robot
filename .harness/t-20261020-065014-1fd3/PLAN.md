# FIXA — 안전·조종 경로 결함 수정

## 목표
B1 B2 B3 B4 B7 B8 B9 B10 B11 B12 B27 B28 B30 B31 B34 B42 B43 을 고치고, 결함마다 재현 시험(tst_*.cpp 또는
tooling/mockbridge/scenarios/safety_*.json)을 남긴다. 검사: `checks\integration.cmd`.

## 방침(요약)
- 움직임 명령(drive 비0 · straight · sendGoal)은 '연결 정상(linkLost 아님) + armed + 맞는 모드' 에서만.
  - straight: 키보드 조종 모드 · 직진 진행 중 아님 · |d| ≤ 2 m · 유한값.
  - sendGoal: 정지 모드가 아님(→ 자율주행으로 바꿈).
- armTeleop(true) 는 linkLost(그 자리에서 다시 계산) 동안 거부. drive 의 NaN/inf 는 0.
- 정지 모드에서 재연결하면 cancel · cancel_goal · 0 Twist 를 다시 보낸다(advertise 뒤, 이벤트 루프 다음 턴).
- 수신이 1 s 넘게 없으면(반쯤 열림 의심) 정지 · 조종을 보낼 때 lastSendDropped 경고.
- --no-publish 에서는 cancelGoal(call_service) 도 안 보낸다(robot 쪽에서 막음 — rosbridge.cpp 는 FIX-B 담당).
- 자율주행에서 벗어나면 cancel_goal.
- 재연결 뒤 3 s 동안은 이전 세션의 하트비트 나이를 보지 않는다(B27). 하트비트만 끊기고 다른 수신이 1 s 안에 있으면 linkLost 아님(B28).
- B30 parseScan 최근접 거리를 base_link 기준 hypot 으로. B31 map 프레임 /plan 은 원문을 두고 map→odom 바뀔 때 다시 투영.
- B34 m_teleop PreciseTimer. B43 Hz 를 header.stamp 간격으로(있으면) 계산.
- QML: 대시보드 페이지가 아니면 조종 키 무시 + 떠날 때 0. 직진·기록 버튼은 연결 없으면 비활성. 팔레트 직진도 같은 조건.

## 진행
- [x] main 빌드 · 재현: main exe(dashboard/build/main_exe)로 safety_*.json 9개 모두 불합격(결함 재현)
- [x] robot.cpp/h 수정
- [x] msgparse(B30), rosbridge stat(B43) 최소 수정. tst_msgparse scanWithMount 기대값을 base 기준(0.9)으로 고침(결함 동작을 기대하던 줄)
- [x] QML 수정(B3 B4 B42) — CommandPalette objectName palette · paletteField(시나리오용)
- [x] 단위 시험 추가(tst_robot · tst_arm)
- [x] 시나리오 safety_*.json 9개 추가
- [x] 기존 시나리오(goal · straight · straight_null)에 연결 대기 + armTeleop 추가(새 안전 규칙)
- [x] 새 단위 시험이 main 소스에서 실패하는지 확인: tst_arm 새 8개 · tst_robot 새 5개 · tst_msgparse scanWithMount 모두 실패
      (main: cmd_vel 평균 간격 109.1 ms, 몰린 수신 32000 Hz, 최근접 0.9 m)
- [x] 수정본으로 safety 9개 + estop · goal · halfopen · normal · straight · straight_null 합격
- [x] checks\integration.cmd 통과: ctest 18/18, pytest 51 passed, 시나리오 합격 31 · 건너뜀 2(파이 필요)
  (첫 회는 tst_robot reconnectClearsLinkLostQuickly 가 재연결이 3 s 안에 될 때 전제가 깨져 실패 → 끊기 전 하트비트 나이 > 3.2 s 를 기다리게 고침)

- [x] 하네스 검사에서 safety_halfopen_estop 불합격: 대시보드 연결이 1.24 s 늦어 비상 정지가 반쯤 열린 지 0.26 s 만에
      눌림(수신 공백 1 s 미만이라 경고 조건 아님). 비상 정지를 t=8 로 옮겨 서버 기준 6~8 s(공백 > 1 s 또는 이미 끊김 — 둘 다 경고해야 함),
      trace 창을 6..10.5 s 로. 구현은 그대로.

## 남은 일 / 메모
- tst_rosbridge 의 타이밍 시험(reconnectResends · rateAndBandwidth · halfOpenReconnects)이 부하 때 가끔 실패했다(다른 작업 빌드 동시 진행).
  이 작업이 바꾼 것은 stamp 가 있을 때의 Hz 뿐이고, 실패한 시험은 stamp 없는 토픽 · 재연결 타이머라 관련이 없다.

## 발견
- 시험 로그·결과는 dashboard/build/ 아래(무시됨)에 둔다. E:\Temp 직접 쓰기는 하네스가 막는다.
