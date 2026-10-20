# FIXB — 연결 판정·큰 지도·성능 결함 수정 (B5 B13 B14 B15 B29 B35 B46 B47)

## 목표
탐색 시험에서 찾은 결함 중 연결 판정(B5 B29)·큰 지도(B13 B14)·trace(B35)를 고치고, 파이 쪽(B15 B46 B47)은
원인 분석·권고를 보고한다(/wheel_cmd 구독 줄이기는 DECISIONS 5). 결함마다 재현 시험을 남긴다.

## 진행
- [x] main 빌드로 재현: B13(3000² 지도마다 GUI 막힘 최대 ~990 ms, gui_ms 28 로 과소), B14(peak 899 MB)
- [x] B5: 내부 QTcpSocket(연결 상태인 것) readyRead 도 수신으로 본다(받는 중인 큰 프레임) + 시간 초과로 끊을 때마다 기준 2 배(12 s 까지, open 에서 3 s)
- [x] B29: 1 s 넘게 붙어 있던 연결이 끊기면 300 ms 뒤 곧 재시도(그 뒤는 3 s 주기). shutdown 뒤에는 재시도 안 함
- [x] B13: 원인 = DashboardPage.qml 의 `Connections { onMessage(topic) }` 가 모든 메시지(9M 칸 지도 포함)를 JS 객체로 변환(300~500 ms).
      RosBridge 에 내용 없는 `received(topic)` 신호를 더하고 QML 은 그것을 쓴다. gui_ms 는 RosBridge 가 잰 dispatch 시간(Stat.guiMs) 포함
- [x] B14: Robot 이 원본 OccupancyGrid QJsonObject(칸당 16 B) 대신 칸 바이트(msgparse::occupancyCells) + info 만 든다. 원문은 변환 뒤 다른 스레드에서 해제. 큰 메시지 파싱 중 UTF-16 원문을 먼저 놓는다
- [x] B35: trace 줄마다 proc(rssMB·peakMB)·render(fps·fpsAvg·fpsMin·frames·stallMs)
- [x] B15: /wheel_cmd 를 화면이 보일 때만 구독하는 변경을 해 봤으나 reconnect 시나리오와 시점 충돌로 되돌림(DECISIONS 5) — 분석·권고만
- [x] B46·B47: 코드 수정 없음 — 분석·권고만(최종 보고)
- [x] 재현 시험: tst_rosbridge(slowBigFrameKeepsConnection·rxTimeoutBacksOff·quickReconnectAfterServerClose),
      tst_robot(bigMapRetainsCompactCopy), 시나리오 perf_bigmap3000·net_flap. 모두 수정 전 코드에서 실패 확인
- [x] checks\integration.cmd 통과 확인(추가 요구 반영 뒤: ctest 18/18, smoke, pytest 51, 시나리오 합격 26 · 건너뜀 2)

## 추가 요구(지도를 RViz2 처럼)
- [x] 지도 레이어 기본 켬(=/map 구독, throttle 2000), Alpha 0.7, Draw Behind 끔. 설정 키를 바꿔 예전 저장값을 읽지 않음(DECISIONS 6)
- [x] Main 의 connectTo(resetSession) 가 SceneView 초기화 전에 mapChanged 를 내 기본 켬이 꺼짐으로 덮이던 경쟁을 mapSynced 로 막음
- [x] 텍스처 min · mag 최근접, mip 없음(scene.mapFilterNearest). 위치 · 셀 중심 정렬은 기존(gridPlane · placeMap)대로, 시험 추가
- [x] '지도 없음 — ...(tooling\pc\map_start.cmd)' 안내(scene.noMapHintShown), 지도를 받으면 사라짐
- [x] 시험: tst_msgparse mapPaletteMatchesRviz2 · cellCenterAlignment, 시나리오 map_rviz · map_grow(--map-grow --map-every 2)

## 발견
- 수정 전 측정: dispatch 317~514 ms 중 Robot::onMap 은 0~30 ms, 나머지는 QML 변환.
- 남은 GUI 막힘(부하 없을 때 ~50~100 ms, 부하 때 ~160 ms): QWebSocket 내부의 27 MB UTF-8 → QString 변환(GUI 스레드). 없애려면 소켓을 다른 스레드로 옮겨야 한다(이번 범위 밖).
- 수정 전/후: 지도 뒤 Robot 이 붙잡는 private 291 MB → 43 MB(단위 시험), 실행 peak 899 → 720~810 MB.
- 프로세스 RSS 는 동시에 도는 다른 작업 부하에 크게 흔들려(447~592 MB) 시나리오 판정에서 뺐다.

## 남은 일
- 없음(검사 통과 확인 뒤 종료). 작업용 산출물은 backup/fixb(무시 폴더).
