## 1. 서버가 큰 메시지를 직렬화하느라 3 s 넘게 아무것도 못 보낼 때(B5 부하 사례)
- options: (a) 받는 중인 바이트만 살아 있음으로 본다 (b) (a) + 시간 초과로 끊을 때마다 기준을 늘린다 (c) 기준 자체를 늘린다(예: 10 s)
- chosen: (b) — 3 s 에서 시작해 끊을 때마다 2 배, 12 s 까지. open()(다른 주소) 때 3 s 로 되돌린다
- why: 서버가 멈춘 동안은 바이트도 pong 도 없어 (a) 만으로는 붙을 때마다 지도 재전송 → 끊김이 끝없이 되풀이된다. (c) 는 정상 링크의 반쯤 열린 연결 감지를 늘 늦춘다
- effect: 같은 서버에서 반쯤 열린 연결이 되풀이되면 두 번째부터 감지가 6·12 s 로 늦어진다. rosbridge.cpp 의 kRxTimeoutMaxMs 를 3000 으로 두면 예전과 같다

## 2. 끊긴 뒤 재연결 시점(B29)
- options: (a) 고정 3 s 유지 (b) 끊긴 직후 즉시 (c) 1 s 넘게 붙어 있던 연결만 300 ms 뒤 곧, 그 뒤는 3 s 주기
- chosen: (c)
- why: 서버가 받자마자 닫는 경우에도 초당 여러 번 붙는 고리에 빠지지 않으면서, 정상 연결이 끊기면 곧 붙는다
- effect: rosbridge.cpp kQuickRetryMs · m_up 판정으로 조절. robot.cpp 의 이벤트 문구 "3 s 마다 재연결" 은 조종 영역(FIX-A) 근처라 그대로 두었다

## 3. B13 의 QML 변환 비용을 없애는 방법
- options: (a) RosBridge 에 내용 없는 received(topic) 신호를 더하고 토픽 이름만 쓰는 QML 을 바꾼다 (b) message 신호를 QML 에서 숨긴다(다른 QML 사용처가 깨짐) (c) 소켓·파싱을 다른 스레드로
- chosen: (a) — DashboardPage.qml 한 줄을 onReceived 로
- why: 막힘의 대부분(300~500 ms)이 이것이고 가장 작은 변경이다
- effect: qml/dash/UserGraphPanel.qml 은 입력란에 포커스가 있을 때만 onMessage(msg)를 받아 그대로 두었다(그때 큰 지도가 오면 여전히 변환 비용이 있다). 남은 ~100 ms 는 QWebSocket 내부 변환이며 (c) 가 필요하다

## 4. B14 메모리 판정을 어디서 할지
- options: (a) 시나리오에서 프로세스 RSS · peak 상한 (b) 단위 시험에서 지도 수신 뒤 private 증가 상한
- chosen: (b) — tst_robot::bigMapRetainsCompactCopy(3000², < 100 MB; 수정 전 291 MB, 후 43 MB)
- why: 동시에 도는 작업 때문에 시나리오 RSS 가 447~592 MB 로 흔들려 수정 전(≈567 MB)과 가르지 못했다
- effect: 시나리오 perf_bigmap3000 은 gui_ms(≤150)·끊김 없음·trace 키(B35)만 본다

## 5. B15 의 /wheel_cmd 구독 줄이기를 할지
- options: (a) 하지 않고 분석·권고만 (b) 대시보드 화면이 보일 때만 구독(Robot::setWheelCmdWanted + DashboardPage.onActiveChanged)
- chosen: (a) — (b) 를 구현·시험했다가 되돌렸다
- why: 시작 4 s 안에 붙지 못하면 연결 화면으로 바뀌는 타이머 때문에 (b) 에서는 재연결 때 구독 집합이 시점에 따라 달라져 기존 시나리오 reconnect(같은 구독 집합 재수신)가 가끔 불합격했다. 또 robot.cpp 는 onMap 쪽만 고치라는 제한이 있다. /wheel_cmd 는 33 Hz 작은 메시지 하나라 파이 CPU 이득도 작다(/scan 이 대부분)
- effect: 구독은 예전과 같다. 다시 하려면 '보이는 화면' 대신 '사용자가 고른 화면' 기준으로 하거나 reconnect 시나리오의 기대를 함께 바꿔야 한다
