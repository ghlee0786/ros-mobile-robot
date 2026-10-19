# 결정 기록 (INT1)

## 1. bigmap: robot.mapSize 가 state 에 없던 것 — 앱 속성을 더할지, 덤프를 고칠지
- options: Robot 에 새 속성 추가 / testhooks 의 JSON 변환에서 QSizeF 를 {width,height} 로
- chosen: testhooks 변환. Robot::mapSize(QSizeF, m 단위)는 원래 있었고 state 에 "QVariant(QSizeF, ...)" 문자열로만 나갔다.
- why: 실제 동작(100×100 m)은 맞고 덤프 형식만 어긋났다. robot.h 를 건드리지 않는다.
- effect: state·trace 의 QSizeF 속성은 모두 {width,height}. 다른 형식(QVector3D 등)은 여전히 문자열 — 필요하면 testhooks.cpp toJson() 에 더한다.

## 2. estop: actions_done 형식 — 실행기를 testhooks 에 맞출지, 그 반대로 할지
- options: 실행기가 {i,t,wall(로드 뒤 초)} 를 해석 / testhooks 가 가짜 대시보드처럼 action 원본 + wall(epoch) 을 남김
- chosen: testhooks 가 action 원본 키(call·set·shot·wait…) + i · t · elapsed(로드 뒤 초) · wall(epoch, 실행 직전)을 남긴다.
- why: 서버 로그는 epoch wall 이라 epoch 이어야 비교된다. 가짜 대시보드(fake_dashboard.py)와 같은 모양이 된다.
- effect: actions_done[].wall 의 뜻이 '로드 뒤 초' → 'epoch 초' 로 바뀜(로드 뒤 초는 elapsed). 이를 읽는 곳은 실행기뿐.

## 3. skew: bridge.clockSkewMs 를 새로 둘지, 시나리오를 topicRows[].clockSkew 로 바꿀지
- options: 시나리오가 토픽 행의 clockSkew(bool) 를 보게 / RosBridge 에 읽기 전용 clockSkewMs 추가
- chosen: RosBridge.clockSkewMs = 최근 60 개 메시지의 header.stamp − 수신 시각 중앙값(ms), 표본 없으면 0. 새 주소로 open() 하면 비운다.
- why: 목표 지시. 숫자로 얼마나 어긋났는지 볼 수 있고 시나리오 기대(>= 7000)를 그대로 쓴다.
- effect: QML 은 아직 쓰지 않는다(화면에 띄우려면 QML 쪽 작업).

## 4. straight_null: 시나리오 기대를 고침 (앱이 맞음)
- options: Robot 이 null 을 0 으로 넣게 / 시나리오가 heading 키가 '없음' 을 기대하게
- chosen: 시나리오 고침 — `{"path":"robot.straightResult.heading","op":"missing"}`. 실행기의 missing 연산이 키가 있을 때 예외(ValueError)로 죽던 것도 실패로 세도록 고쳤다.
- why: P4 는 일부러 null 키를 빼고 QML 이 '—' 를 보인다(tst_robot::straightNullOmitted). 0 은 '측정했더니 0°' 와 구별되지 않는다.
- effect: 제목도 'null 이면 heading 키 없음' 으로 바꿨다.

## 5. freeze · halfopen: 장애 시각의 기준을 '서버 시작' → '첫 연결' 로
- options: 시나리오의 장애 시각을 늦춘다(여전히 부하에 따라 경합) / 모의 서버의 장애 시계를 첫 클라이언트 연결에서 시작
- chosen: 모의 서버 Faults 의 시계가 첫 연결에서 시작(--freeze · --halfopen · --script). 실행기는 trace 의 t 를 서버 로그의 첫 connect wall 기준으로 바꿔 판정한다.
- why: 병렬 실행에서 대시보드는 서버보다 2~10 s 늦게 붙었다(실측 4.2 s · 7.5 s). '5 s 에 반쯤 열기' 가 연결이 없을 때 지나가 버려 halfopen 이 아무것도 시험하지 않았다.
- effect: `mock_rosbridge.py --freeze S@T` 등은 '첫 연결 T 초 뒤'. 서버만 띄우는 수동 시험도 첫 클라이언트 기준. --disconnect-every · --flood 는 그대로(서버 시작부터).

## 6. freeze: rxAge 를 pong 을 뺀 '메시지 수신' 기준으로
- options: 그대로(하트비트를 한 번이라도 받았을 때만 linkLost) / rxAge 를 메시지만으로 재고, 반쯤 열린 연결 판단(3 s 무수신 → abort)은 pong 포함 그대로
- chosen: 두 시계로 나눔 — m_lastRx(pong 포함, abort 용) · m_lastData(메시지만, rxAge · linkLost 용).
- why: 송신이 멈춘 rosbridge 도 WebSocket 층은 ping 에 계속 답해 rxAge 가 1 s 를 넘지 않았다. 하트비트를 받기 전에 멈추면 linkLost 가 영영 켜지지 않았다(재현: tst_robot::linkLostWhenDataStopsButPongs).
- effect: 데이터가 3 s 멈추면 연결은 유지한 채 linkLost(빨간 테두리). rxAge 를 QML 에서 쓰는 곳은 없다.

## 7. soak: 메모리 증가 기준 — 기준 표본에서 시작 구간을 뺀다 (50 MB 상한은 그대로)
- options: 상한을 올린다 / 누수를 찾아 고친다 / 시작 직후 표본을 기준에서 뺀다
- chosen: 실행기가 처음 mem_warmup_s(기본 30 s)의 표본을 빼고, 남은 표본 앞 1/5 의 최소 → 마지막 증가를 본다. 결과에 mem_growth_mb 도 남긴다.
- why: 20261020_024216 soak 실측은 0 s(QML 로드 전) 51 MB → 10 s 307 MB → 120 s 247 MB → 190~603 s 194~199 MB 평탄. 예전 판정의 '144 MB 증가' 는 로드 전 표본 51 MB 를 기준으로 삼은 것이고, 실제로는 10 분 동안 줄었다. 커질 수 있는 구조도 확인: trail 20000 점 상한, events 100, rxHistory · latencyHistory 60, skew 표본 60, arrivals 2 s 창 — 무제한 구조 없음.
- effect: 진짜 누수(예: 분당 12 MB)는 여전히 잡는다(test_mem_growth_skips_warmup).

## 8. 조종 잠금(P6, main): 시나리오에서 armTeleop 을 어떻게 부를지
- options: 지금 worktree 에 없는 robot.armTeleop 을 그대로 부름(이 빌드에서 종료 코드 3) / 실행기가 몰래 넣음 / action 에 "optional": true 를 두어 없는 메서드는 건너뜀
- chosen: normal · estop 맨 앞에 `{"wait":"bridge.connected","timeout_s":10}` + `{"call":"robot.armTeleop","args":[true],"optional":true}`. testhooks 는 optional call 의 메서드가 없으면 로그만 남기고 건너뛴다.
- why: 지금 worktree(잠금 없음)와 main 병합 뒤(연결 직후 자동 잠금) 모두에서 같은 시나리오가 맞게 돈다. flood 는 잠겨도 m_teleop 이 0 Twist 를 10 Hz 로 계속 보내(main robot.cpp) 판정(10 Hz±10%)에 영향이 없어 넣지 않았다.
- effect: main 병합 뒤 normal 의 distance > 0.2 가 다시 의미를 갖는다. optional 을 빼면 메서드가 없을 때 실패한다.

## 9. 상대 shot 경로
- options: 시나리오 파일을 절대 경로로 / 실행기가 actions.json 을 쓸 때 결과 폴더 기준 절대 경로로
- chosen: 실행기에서 바꾼다(메인 세션 지시). 저장소 루트에 생긴 map_top.png 는 지웠다.
- effect: 시나리오 파일은 그대로 상대 경로.

## 10. trace 에 연결 변화 줄 더하기
- options: trace 주기를 올린다 / bridge.connectedChanged 때마다 한 줄 더 쓴다
- chosen: 바뀔 때마다 한 줄 더(끝난 뒤 — 앱 종료로 닫히는 것 — 는 쓰지 않음). 모든 trace 줄에 wall(epoch)을 넣었다.
- why: halfopen 의 끊김 → 재연결 간격은 재시도 타이머 위상에 따라 0~3 s 라 2 Hz 표본이 놓칠 수 있다.

## 11. goal 경합: 모의 서버가 map→odom 을 /map_odom 으로도 보냄
- options: 시나리오가 robot.mapOdomFresh 를 기다림 / 모의 서버를 실제 구성처럼 /map_odom 5 Hz 로
- chosen: 모의 서버(sim.py)가 --map-odom 이나 --nav 이면 /map_odom 을 5 Hz 로 발행(/tf 쪽은 그대로).
- why: 전체 실행에서 goal 이 '/goal_pose 프레임 odom' 으로 한 번 떨어졌다. map→odom 이 throttle 200 ms · queue 1 인 /tf 에서만 와서 33 Hz odom tf 에 덮였고, 8.4 s 뒤에야 처음 살아남았다(sendGoal 은 6 s). 실제 PC 내비 스택은 tooling/nav/map_odom_pub.py 로 /map_odom 을 따로 보내고 대시보드도 그것을 구독한다 — 모의 서버만 빠져 있었다.
- effect: goal 3 회 연속 합격. /tf 로만 오는 경우는 tst_robot 이 다룬다.

## 12. estop zero_after_action: 같은 TCP 묶음으로 온 직전 명령
- options: after_ms 를 늘린다(느슨) / testhooks wall 을 앞당긴다(묶음 안 순서는 못 가림) / 서버 로그의 스트림 순서를 쓴다
- chosen: 실행기가 action 뒤 첫 0 명령을 찾아, wall >= action 이라도 스트림 순서로 그보다 앞선 비0 은 action 전에 보낸 것으로 본다. 그 뒤의 비0 은 그대로 실패.
- why: 20261020_042326 에서 estop 직전 10 Hz 타이머의 0.08 이 estop 의 0 명령들과 한 묶음으로 와 wall 이 0.6 ms 늦었다(순서는 0.08 → 0 × 3). TCP 는 보낸 순서를 지킨다.
- effect: test_runner::test_zero_after_action_same_batch. after_ms 가 음수인 기존 판정은 그대로.
