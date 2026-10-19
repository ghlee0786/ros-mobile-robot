## 1. 목표(cmd) 계열을 계단으로 그리는 방법
- options: (a) PlotSeries 에 steps 속성을 더하고 PlotFeeder 가 decimate(steps) 를 쓰게 함(src/plot 2곳 수정) · (b) QML 에서 같은 시각에 이전 값 점을 하나 더 push
- chosen: (a)
- why: decimate 에 steps 인자가 이미 있으나 쓸 길이 없었다. (b)는 통계(count · hz)를 왜곡한다.
- effect: PlotSeries.steps(기본 false) 가 생겼다. 되돌리려면 plotseries.h 의 속성과 plotfeeder.cpp visiblePoints 의 인자만 빼면 된다.

## 2. 잠금 해제 방법(요구서 '0.6 s 길게 누르기' vs 메인 지시 '한 번의 클릭/키')
- options: 길게 누르기만 · 한 번 클릭만 · 길게 누르기 버튼 + Ctrl+Enter 키
- chosen: 버튼은 0.6 s 길게 누르기, 키보드는 Ctrl+Enter 한 번
- why: 버튼 실수 해제를 막으면서 키보드 조종 흐름은 키 하나로 이어지게.
- effect: 잠긴 채 조종 키 · 화면 패드 · 조이스틱을 누르면 안내 문구가 3 s 뜬다. 화면 ↑↓←→ 패드와 조이스틱도 같이 잠긴다(drive() 에서 버림). 비상 정지는 잠금과 무관.

## 3. '30 s 무입력' 의 뜻
- options: drive() 호출만 입력으로 봄 · 0 이 아닌 명령을 유지하는 동안도 입력으로 봄
- chosen: 둘 다 입력(키를 누르고 있는 동안은 잠그지 않음)
- why: 한 방향 키를 30 s 넘게 누르고 있을 때 갑자기 서면 안 된다.
- effect: robot.cpp m_teleop 타이머 안. 시험은 armIdleMs 를 줄여 확인한다.

## 4. CSV · PNG 저장 폴더
- options: Robot 에 Q_INVOKABLE recordsDir() 추가 · src/plot exportCsv 가 폴더를 만들게 수정 · modelFile URL 에서 추론
- chosen: Robot::recordsDir()(dataDir/records 를 만들고 경로를 돌려준다)
- why: PNG(grabToImage)도 같은 폴더가 필요하고 dataDir() 이 QML 에 열려 있지 않았다.
- effect: 파일 이름 `<wheel|imu|user_graph>_yyyyMMdd_HHmmss.csv/.png`.

## 5. 기본 그래프 · 사용자 그래프 데이터
- options: 기본 그래프도 경로 구독 · Robot::sampled push
- chosen: 기본 바퀴 · IMU 는 sampled(30 Hz) push, 경로 구독은 사용자 그래프에만(비판 수정안 4)
- why: G7 throttle 하한과 별개로 기존 구독을 늘리지 않는다.
- effect: 사용자 그래프의 PlotSeries 는 throttleMs 기본 -1(센서 기본값) 그대로.

## 6. 시나리오 json
- options: 요구서대로 tooling/mockbridge/scenarios/dash_*.json 작성 · 메인 지시대로 만들지 않음
- chosen: 만들지 않음(모의 서버 미병합). 대신 .harness/<id>/dash_check.py 가 가짜 rosbridge 로 같은 판정을 한다.
- effect: PLAN.md '남은 일' 에 통합 단계 시나리오를 적었다.
