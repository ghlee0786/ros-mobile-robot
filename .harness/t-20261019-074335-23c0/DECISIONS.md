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

## 5. Nav2 목표를 보내는 방법
- options: rosbridge 액션(send_action_goal) · `/goal_pose` 토픽 발행
- chosen: `/goal_pose` (geometry_msgs/PoseStamped) 발행, 취소는 `/navigate_to_pose/_action/cancel_goal` 서비스(0 uuid = 전체)
- why: Pi 의 rosbridge 는 Foxy 판이라 액션 op 가 없다. bt_navigator 는 `goal_pose` 를 듣고 받으면 NavigateToPose 를 시작한다(RViz 2D Goal Pose 와 같은 경로).
- effect: 진행 상황 · 결과는 받지 않는다(계획 경로 `/plan` 은 이미 그린다). 액션을 쓰려면 `Robot::sendGoal` 한 곳만 바꾼다.

## 6. 목표 좌표 프레임과 모드
- options: odom 그대로 · map→odom TF 가 있으면 map 으로 변환 / 모드 유지 · 자율주행으로 전환
- chosen: map→odom 이 있으면 map 프레임으로 변환, 없으면 odom. 보낼 때 모드를 `자율주행` 으로 바꾼다
- why: Nav2 global_frame 이 map 이다. Nav2 속도 출력이 `/teleop/cmd_vel`(tooling/nav/nav2_params_robot.yaml)이라 키보드 조종의 10 Hz 0 속도가 Nav2 를 덮어쓴다.
- effect: `Robot::sendGoal`. 키보드 조종으로 돌아오면 Nav2 와 겹치므로 `정지` 를 누르면 목표가 취소된다.

## 7. 이동 목표 화살표 조작
- options: 클릭 두 번(위치 · 방향) · 누른 채 끌기(RViz)
- chosen: 누른 곳 = 위치, 끄는 쪽 = 바라볼 방향, 놓으면 전송 후 도구 꺼짐(RViz 와 같음). 끌지 않으면 로봇의 지금 방향
- why: 사용자가 "rviz 방식" 을 지정했다.
- effect: `SceneView.qml` MouseArea 의 goalDragging 처리.

## 8. Ctrl · Shift · Alt 조작 배치
- options: Fusion 360 기본(가운데 이동 · Shift+가운데 회전 · 휠 확대)만 · 가운데 버튼 없는 마우스/터치패드용 왼쪽 버튼 조합 추가
- chosen: Fusion 기본 + 왼쪽 끌기 회전(Shift 도 같음) · Ctrl+왼쪽 이동 · Alt+왼쪽 확대
- why: 사용자가 Fusion 방식과 Ctrl · Shift · Alt 를 함께 말했다. Fusion 은 Shift 만 쓰므로 나머지 둘을 이동 · 확대에 붙였다. 기존 자유 시점의 왼쪽 끌기 회전도 그대로 둔다.
- effect: `SceneView.qml` MouseArea `onPressed` 의 mode 결정 한 줄.

## 9. 추적 · 위에서 시점에서 조작했을 때
- options: 추적 · 위에서는 조작 무시 · 지금 각도 그대로 자유 시점으로 넘어감
- chosen: 지금 각도 그대로 자유 시점으로 넘어감(뷰 큐브 · 메뉴도 같음)
- why: 3D 화면 어디서나 조작이 먹어야 한다는 요청. 화면이 튀지 않게 각도를 이어받는다.
- effect: `enterFree()`. 다시 추적하려면 `추적` 버튼.

## 10. 뷰 큐브의 기준
- options: 로봇 기준(앞 = 로봇이 보는 쪽) · 세계(odom) 기준
- chosen: odom 기준(앞 = +x, 왼쪽 = +y), 위 뷰는 Fusion 처럼 `앞` 이 화면 아래. 자유 시점 각도(camYaw/Pitch/Dist)도 저장
- why: Fusion 의 ViewCube 처럼 장면 좌표에 붙어 있어야 회전해도 의미가 같다. 출발 때는 로봇 앞 = odom +x 라 처음엔 같다.
- effect: `SceneView.qml` 의 `views` 표와 Face 배치.
