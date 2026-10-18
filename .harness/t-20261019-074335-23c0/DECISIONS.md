# DECISIONS

## 1. CAD(0.3135 m)와 설정(0.34 m)의 바퀴 간격 차이
- options: 바퀴를 CAD 위치에 그림 · 바퀴를 설정값 위치에 그림 · 몸체를 y 방향으로 8% 늘려 맞춤
- chosen: 바퀴를 설정값 ±0.17 m 에 그리고 몸체는 CAD 그대로
- why: 사용자가 실측 설정을 따르라고 했고, 계산해 보니 0.34 m 에서 바퀴 안쪽 면(0.144 m)이 몸체의 차축 부근 바깥 끝(0.1437 m)과 맞닿아 틈 · 겹침이 없다. 몸체를 늘리면 LiDAR 등이 찌그러진다.
- effect: `qml/RobotModel.qml` 의 `wheelSeparation` 하나로 바뀐다.

## 2. LiDAR 헤드 회전 속도
- options: 실제 회전수(≈12 Hz) · 눈에 잘 보이게 느리게 돌리기
- chosen: 실제 회전수 — `scan_time` 의 역수(없으면 /scan 수신 Hz), /scan 이 꺼지거나 끊기면 멈춤
- why: 첨부 범위가 "scan frequency ~10-12 Hz from the scan messages" 로 정했다. 60 fps 화면에서는 한 프레임에 약 70° 씩 돌아 실제처럼 빠르게 보인다.
- effect: 느리게 보이고 싶으면 `RobotModel.qml` 의 `FrameAnimation` 에 배율을 곱하면 된다. 방향은 위에서 보아 시계 방향(RPLIDAR).

## 3. 측정 도구의 형태
- options: 바닥 두 점 거리 · 로봇 기준 거리 · 각도 측정까지
- chosen: `거리 측정` 버튼을 켜고 바닥을 두 번 클릭 → 거리 · Δx · Δy(odom 프레임 m). 측정 중에는 자유 시점 드래그 대신 클릭을 받는다
- why: 1차 범위의 "측정 도구" 에 가장 작은 형태. LiDAR 점 · 경로와 같은 바닥 좌표라 장애물 간격 확인에 바로 쓴다.
- effect: `SceneView.qml` 의 `measureA/B` · MouseArea `onClicked` 에서 바뀐다.

## 4. 저장할 "레이아웃 · 표시 설정"
- options: 창 위치 · 크기 · 카메라 · 레이어 · 센서 구독 · 자유 시점 각도 등
- chosen: 카메라 모드 · 4개 레이어 체크 · 창 크기 (QtCore `Settings`, 기존 주소 저장과 같은 QSettings)
- why: 화면에서 사용자가 바꾸는 표시 항목이 이것들이다. 창 위치는 모니터 구성이 바뀌면 화면 밖에 뜰 수 있어 뺐다. 센서 구독은 연결 시험 성격이라 매번 기본(카메라 외 전부 켬)으로 시작한다.
- effect: 항목을 더하려면 `Settings { property alias … }` 한 줄씩 추가한다.
