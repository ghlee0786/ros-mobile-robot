# 결정 기록 (P4 코어)

## 1. 토픽 하한(G7)을 '기록만' 원하는 경우에도 적용할지
- options: (a) 요구서 (2) 대로 이유가 record 뿐이면 0(전속) / (b) G7 대로 항상 max(하한, …)
- chosen: (b) 항상 하한 적용 — /imu/data_raw 50 · /tf 200 · /map 2000 · /camera/image_raw 1000, /tf_static 은 항상 0
- why: 메인 세션 지시(G7)가 우선이고, 기록만 켜도 전속 IMU 가 파이 rosbridge 병목(00908e1)을 다시 만든다.
- effect: IMU 를 기록하면 CSV · raw.jsonl 도 20 Hz 다. 바꾸려면 rosbridge.cpp 의 `kThrottleFloor` 와 `subOf()` 를 고친다.

## 2. throttleMs = -1 의 뜻
- options: -1 을 0 과 같게 / -1 = 의견 없음(최소값 계산에서 빠짐)
- chosen: -1 = 의견 없음(record 와 같은 취급), 하한은 그대로 적용
- why: P5 의 기본 -1("센서 기본값")이 다른 이유의 throttle 을 낮추지 않게.
- effect: `RosBridge::subOf()` 한 곳.

## 3. mapOdomFresh 판정 시간
- options: 2 s(요구서) / 10 s(비판 수정안)
- chosen: 10 s (`kMapOdomFreshMs`)
- why: /tf throttle 200 + queue 1 에서 map→odom 은 평균 0.66 Hz 로 불규칙하게 와 2 s 면 약 24 % 동안 낡음으로 본다.
- effect: robot.cpp 의 상수 하나. 실제 수신율은 파이에서 재야 한다.

## 4. raw.jsonl meta 의 "started" 형식
- options: ISO 문자열 / epoch 초(double)
- chosen: epoch 초(double) — 각 줄의 "wall" 과 같은 단위
- why: --replay 쪽이 같은 시계로 계산하기 쉽다.
- effect: P1 --replay 가 다른 형식을 기대하면 `Robot::toggleRecording()` 의 meta 한 줄만 고친다.

## 5. topicRows · TopicModel 의 kbps 단위와 형식
- options: 문자열("12.3 kbps") / 숫자, kB/s / kbit/s
- chosen: 숫자, kbit/s(소수 1 자리). latencyMs 가 없으면 -1(NaN 은 매번 dataChanged 를 일으켜 쓰지 않음)
- why: 'kbps' 이름의 관례(킬로비트)와 모델 정렬 · 비교의 편의.
- effect: `Robot::updateStatus()` 의 kbps 계산 한 줄.

## 6. 끊긴 상태의 조종(drive) 알림 빈도
- options: 누를 때마다 / 끊긴 동안 한 번
- chosen: drive 는 끊긴 동안 한 번(lastSendDropped 가 false 일 때), estop 은 누를 때마다. 다시 연결되면 lastSendDropped=false.
- why: 키 반복 입력으로 이벤트 목록이 넘치지 않게.
- effect: `Robot::drive()` 의 조건 한 줄.

## 7. mapBinary 속성 형태
- options: bool + 고정 임계값 / int 임계값 속성
- chosen: bool `mapBinary`(임계값 65 — 이전 지도 그리기와 같은 값)
- why: 요구서는 "binary 보기 속성"만 요구. 함수 `occupancyToImage` 는 임계값을 인자로 받으므로 나중에 int 속성으로 늘리기 쉽다.
- effect: robot.h 의 속성 하나, robot.cpp 의 `kMapBinaryThreshold`.

## 8. 큰 메시지 GUI 막힘 시험의 모의 서버 위치
- options: 같은 스레드 / 다른 스레드
- chosen: tst_rosbridge 의 큰 메시지 시험만 모의 서버를 다른 스레드에 둔다.
- why: 같은 스레드면 8 MB 를 '보내는' 비용(실제로는 파이가 함)이 GUI 간격에 섞여 92~159 ms 가 나왔다. 클라이언트 쪽만 재면 22~28 ms.
- effect: tests/tst_rosbridge.cpp 의 bigMessageOffThread 하나.

## 9. /tf_static queue_length 를 어디서 정할지
- options: Robot 의 want 호출에 queueLength 10 을 넘김 / RosBridge 가 토픽 규칙으로 항상 10 이상
- chosen: RosBridge `subOf()` 에서 /tf_static 은 항상 queue 10 이상(throttle 0 과 같은 자리)
- why: 어느 묶음이 /tf_static 을 원해도 정적 변환이 사라지지 않게(G7 하한 표와 같은 방식).
- effect: rosbridge.cpp 한 줄.

## 10. 스캔 frame_id 가 base_link 일 때 장착 변환
- options: base_link→base_link 변환을 찾음(없으니 항등) / base_link 면 명시적으로 항등
- chosen: 명시적으로 항등 — base_link→laser 변환이 있어도 적용하지 않는다
- why: 실기 /scan 은 이미 base_link 기준이라 laser 변환을 또 적용하면 12 cm 어긋난다.
- effect: `Robot::onScan()` 한 줄.
