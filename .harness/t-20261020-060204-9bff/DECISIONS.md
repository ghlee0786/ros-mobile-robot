# 결정

## 1. LiDAR 토픽을 언제 정하나 (토픽 목록 전 /scan 을 먼저 구독할지)
- options: (a) 처음엔 /scan 을 구독하고 목록이 오면 /scan_compact 로 바꿈 · (b) 목록을 받을 때까지 LiDAR 를 구독하지 않고, 연결 6 s 뒤에도 목록이 없으면 /scan
- chosen: (b)
- why: 목표가 "/scan 은 구독하지 않음" 을 서버 rx 로그로 판정하라고 하고, 잠깐의 /scan 구독도 파이 rosbridge 에 큰 메시지를 보내게 한다. 목록은 연결 직후 오므로 지연은 수십 ms.
- effect: rosapi 가 응답하지 않는 파이에서는 LiDAR 가 연결 6 s 뒤부터 보인다. `Robot::pickScanTopic` 의 6000 ms 로 바꾼다.

## 2. normal.json 의 "LiDAR 구독" 규칙
- options: (a) 규칙을 /scan_compact 로 바꿈 · (b) normal 에 --no-scan-compact 를 줘서 /scan 을 유지
- chosen: (a)
- why: 모의 서버 기본값이 이제 파이와 같이 /scan_compact 를 내므로 정상 시나리오도 그 경로를 봐야 한다. 강도는 같다(구독 1 회 이상). /scan 경로는 scan_fallback 이 본다.
- effect: `scenarios/normal.json` 한 줄. 되돌리려면 topic 을 /scan 으로 하고 server_args 에 --no-scan-compact.

## 3. insp_params 의 --no-publish 판정
- options: (a) 실행기에 대시보드별 인자 기능을 더해 한 시나리오에서 두 대시보드 · (b) 시나리오 둘(insp_params, insp_params_nopub)
- chosen: (b)
- why: 실행기 기능을 늘리지 않고 판정이 서버 로그 하나로 깔끔하다.
- effect: 시나리오 수 +1.

## 4. --diag-stale 기준 시각
- options: (a) 서버 시작 기준 유지 · (b) 다른 장애처럼 첫 연결 기준
- chosen: (b)
- why: 대시보드는 서버보다 2~10 s 늦게 붙어, 서버 시작 기준 3 s 면 진단을 한 번도 받지 못해 STALE 을 판정할 수 없다. 실행기 문서도 "장애 시각은 첫 연결 기준".
- effect: `sim.make_diag` 와 도움말. 첫 연결 전에는 계속 낸다.

## 5. 초기 위치 도구 시나리오의 입력 방법
- options: (a) 시험 훅에 마우스 끌기 동작 추가 · (b) scene.tool 을 '초기 위치' 로 두고 도구가 부르는 `robot.setInitialPose` 를 직접 부름(기존 run_scene_test 와 같음)
- chosen: (b)
- why: 범위 밖 기능(마우스 주입)을 늘리지 않는다. 판정 대상(/dashboard/initialpose 프레임 · 값 · 공분산)은 같다.
- effect: 마우스 → 좌표 변환은 시나리오가 보지 않는다(기존 tst · 수동 확인 몫).

## 6. '시계 차' 부호 표기
- options: (a) 절댓값 "±n" 고정 문자 · (b) 부호 있는 값 "+n / −n"
- chosen: (b)
- why: 파이 시계가 빠른지 느린지가 바로 보인다.
- effect: `TopicsTab.qml` 요약 줄 한 줄.

## 7. main(FIX-A) 병합 뒤 겹친 `Robot::m_connClock`
- options: (a) FIX-A 의 멤버를 남기고 이 브랜치 것을 지움 · (b) 이 브랜치 것을 다른 이름으로 따로 둠
- chosen: (a)
- why: 둘 다 "이번 연결 뒤 경과" 로 같은 뜻이고 같은 곳(연결됨)에서 시작한다. 시계를 둘 둘 이유가 없다.
- effect: `pickScanTopic` 의 6 s 판정이 FIX-A 링크 판정과 같은 시계를 쓴다. 나누려면 robot.h 에 멤버를 하나 더 두고 연결 시 start.

## 8. normal 시나리오가 병렬 창의 포커스 빼앗기로 멈추는 문제
- options: (a) DashboardPage 의 '포커스를 잃으면 drive(0,0)' 을 키를 누른 경우로만 좁힘 · (b) 시험 창을 활성화하지 않고 띄움 · (c) 시나리오가 drive 를 0.5 s 마다 다시 줌(조이스틱 갱신처럼)
- chosen: (c)
- why: 사용자 지시대로 안전 조건은 약하게 하지 않는다. (b) 는 키 입력 시나리오(safety_page_keys)의 포커스에 영향을 줄 수 있다. 판정 기준(거리 > 0.2 m · 자취 > 10)은 그대로.
- effect: `scenarios/normal.json` 의 actions 만. 포커스를 잃은 뒤 최대 0.5 s 안에 다시 움직인다.
