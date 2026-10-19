# 결정 기록 (FIXA)

## 1. straight · sendGoal 의 '맞는 모드' 를 무엇으로 볼 것인가
- options: (a) straight 는 키보드 조종 모드만, sendGoal 은 정지 모드만 아니면 / (b) 둘 다 모드 제한 없이 잠금만 / (c) 확인 대화상자
- chosen: (a). 둘 다 armed · 링크 정상 · 직진 진행 중 아님을 함께 요구한다. sendGoal 은 지금처럼 자율주행으로 바꾼다.
- why: 안전 원칙('연결 정상 + armed + 맞는 모드')을 그대로 따르고, 자율주행 중 직진(B12)과 정지 모드에서 sendGoal 이 모드를 바꾸는 것(B2)을 한 번에 막는다. 직진 중 sendGoal 도 같은 경합이라 막는다.
- effect: 자율주행에서 목표를 보내려면 먼저 잠금을 풀어야 한다(무입력 30 s 뒤 다시 잠김). 바꾸려면 robot.cpp `Robot::straight` · `Robot::sendGoal` 의 거부 조건을 고친다.

## 2. 반쯤 열린 연결의 경고(lastSendDropped) 기준
- options: (a) 수신이 1 s 넘게 없을 때 정지 · 조종을 보내면 경고 / (b) linkLost(3 s) 일 때만 / (c) rosbridge send() 결과만
- chosen: (a) — 연결이 끊겼거나 rxAge > 1 s.
- why: 반쯤 열린 동안 send() 는 true 를 돌려주고(FIX-B 범위라 그대로), 평소에는 33 Hz 토픽이 들어와 rxAge 가 0.1 s 안쪽이다. 3 s 를 기다리면 그 사이 누른 정지에 경고가 없다.
- effect: 센서를 모두 끈 채 정지를 누르면 경고가 뜰 수 있다. `Robot::noteDropped` 의 1.0 을 바꾸면 된다.

## 3. 재연결 때 다시 보내는 범위
- options: (a) 모드가 '정지' 이면 재연결마다 0 Twist ×3 · /straight_drive/cancel · cancel_goal / (b) 끊긴 동안 누른 정지가 있을 때만
- chosen: (a)
- why: 정지 명령이 닿았는지는 어느 쪽이든 알 수 없고, 화면이 '정지' 면 로봇도 멈춰 있어야 한다. 다시 보내도 해가 없다.
- effect: 정지 모드에서 재연결할 때마다 cancel 이 한 번 더 나간다. advertise 가 먼저 나가도록 이벤트 루프 다음 턴에 보낸다.

## 4. 하트비트만 끊긴 경우(B28)
- options: (a) 하트비트 3 s 공백 + 다른 수신도 1 s 넘게 없을 때만 linkLost / (b) 지금처럼 하트비트 단독 / (c) 하트비트 판정 제거
- chosen: (a). 그리고 재연결 뒤 3 s 동안은 이전 세션의 하트비트 나이를 보지 않는다(B27).
- why: rosbridge 는 TCP 하나로 오므로 다른 토픽이 들어오면 링크는 살아 있다. 하트비트 노드만 죽은 것을 링크 끊김으로 보면 주행 중 잠긴다. 링크 전체가 멈추면 rxAge 로 3 s 안에 잡힌다.
- effect: `Robot::linkDown` 한 곳. 하트비트를 다시 단독 조건으로 하려면 `rx > 1.0` 조건을 지운다.

## 5. B43 Hz 계산
- options: (a) header.stamp 가 있으면 stamp 간격으로 Hz 를 계산 / (b) 고정 2 s 창의 개수 / (c) 표시만 평활
- chosen: (a). stamp 가 없거나 간격이 0 이하이면 지금처럼 수신 시각으로.
- why: GUI 가 멈춘 뒤 몰려 받은 메시지는 수신 시각이 몰려 있지만 stamp 는 실제 발행 간격이다. (b) 도 몰린 묶음이 창에 들어오면 부푼다.
- effect: rosbridge.cpp `stat()` · `dispatch()` 의 몇 줄(FIX-B 의 시간 초과 · 재시도 타이머와는 다른 곳). 재생(--replay --rate 2)에서는 원래 발행 주기로 보인다.

## 6. B3 · B4 · B42 시나리오의 키 · 팔레트 조작 방식
- options: (a) 팔레트는 objectName(palette · paletteField)으로 열고 글자 넣고 runCurrent 를 부른다, 페이지 키는 창 활성화 뒤 key 동작 / (b) 둘 다 창 활성화 + 키
- chosen: (a)
- why: 시나리오가 2 개씩 동시에 돌아 창 활성화를 빼앗길 수 있다. 팔레트는 키 없이 같은 run 경로를 탈 수 있다. 페이지 키(B4)는 키 처리 자체가 결함이라 키로 시험하되, 페이지 0 에서 비0 이 나가는 대조(count_ge)를 함께 둬 키가 안 들어간 경우를 합격으로 치지 않는다.
- effect: CommandPalette.qml 에 objectName 두 줄.
