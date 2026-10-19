# 결정 기록 (P5 플롯 엔진)

## 1. PlotSeries.throttleMs 기본 -1 을 지금의 RosBridge 에 그대로 넘길지
- options: (a) -1 을 그대로 want() 에 넘긴다 (b) P4 병합 전까지 -1 이면 0 으로 바꿔 넘긴다
- chosen: (a)
- why: G7 약속(P4 가 토픽별 하한 표로 -1 = 센서 기본값을 해석)과 맞춘다. 지금 rosbridge.cpp 는 -1 을 throttle_rate=-1 로 보내고 기존 throttle 을 덮어쓴다 — P4 병합으로 풀릴 문제다.
- effect: P4 병합 전 앱에서 사용자 그래프가 같은 토픽을 구독하면 throttle 이 풀릴 수 있다. 바꾸려면 plotseries.cpp 의 resubscribe()/setThrottleMs() 에서 넘기는 값만 고친다.

## 2. 경로가 여러 값(배열 · 슬라이스 · 필터)을 낼 때 선 하나에 무엇을 넣을지
- options: 첫 값 / 평균 / 여러 선으로 나눔
- chosen: 첫 값
- why: PlotSeries 는 선 하나다. 여러 선은 P6 가 경로마다 PlotSeries 를 만들면 된다.
- effect: plotseries.cpp onMessage() 마지막 줄만 바꾸면 된다.

## 3. zoomOut(horizontal, vertical) 의 뜻
- options: 2 배 축소 / 원래 보기로 되돌림
- chosen: 되돌림 — 가로는 일시정지를 풀어 실시간 창으로, 세로는 yMin/yMax 고정을 풀고 autoY 를 켠다
- why: PlotJuggler 의 zoom out(맞춤)과 같은 동작이고, 배율 축소는 zoomTo 로 할 수 있다.
- effect: plotfeeder.cpp zoomOut() 에서 바꾼다.

## 4. zoomTo · pan(dx≠0) 이 일시정지를 거는지
- options: 건다 / paused 는 바깥이 정한다
- chosen: 건다(실시간이면 다음 타이머에 창이 덮어써지므로). 바깥에서 xMin/xMax 를 직접 쓰는 것(링크 줌)은 paused 를 바꾸지 않는다.
- effect: plotfeeder.cpp zoomTo()/pan() 의 setPaused(true) 를 빼면 된다.

## 5. CSV 의 receive time · header.stamp 열
- options: 점마다 수신 시각 · stamp 를 따로 저장 / 그래프 시각 하나로 채움
- chosen: 그래프 시각 하나로 채움 — receive 기준이면 receive time 열(앱 시작 기준 초), header 기준이면 header.stamp 열(그래프 시각 - 오프셋 = 원 stamp)을 채우고 다른 열은 비운다
- why: 버퍼를 QPointF 하나로 유지해 메모리 · 코드를 늘리지 않는다.
- effect: 둘 다 필요하면 TimeBuffer 에 열을 더해야 한다.

## 6. 시험 위치 · 이름
- options: 요구서의 src/plot/tests + test_plot.cmd / 메인 지시의 tests/tst_plot_*.cpp
- chosen: 메인 지시(우선) — tests/tst_plot_{messagepath,timebuffer,transforms,feeder}.cpp, test_plot.cmd 는 만들지 않음. QXYSeries 시험은 QTEST_MAIN + offscreen 에서 C++ QLineSeries 로 한다. rosbridge.cpp 를 직접 link 하지 않고 W0 의 dashboard_core 를 쓴다.
