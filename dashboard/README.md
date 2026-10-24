# 로봇 대시보드 (Qt 6 Quick 3D · C++)

Pi 의 rosbridge(`ws://<로봇>:9090`)에 붙어 바퀴 속도 · 오도메트리 · IMU · LiDAR 를 한 화면에 보이고,
로봇 3D 모델이 오도메트리대로 움직이는 Windows 데스크톱 앱. 화면 구성은 와이어프레임
(Main / Inspector / Connect / Storyboard)을 따른다.

## 빌드 · 실행

```
dashboard\build.cmd     # Qt 6.8.3 MinGW (E:\Qt) · CMake · Ninja → dashboard\build\robot_dashboard.exe
dashboard\run.cmd       # 없으면 빌드 후 실행 (model\ · records\ 는 dashboard\ 기준)
```

Qt 를 다른 곳에 깔았으면 `set QT_ROOT=<Qt 폴더>` 뒤에 실행한다. 필요한 Qt 모듈: Quick, Quick3D, WebSockets, Graphs.

## 화면

- **대시보드** — 3D 뷰(카메라 추적 · 위에서 · 자유, 레이어 LiDAR 점 · 이동 경로 · 좌표축 · 지도, `거리 측정` 을 켜고 바닥을
  두 번 클릭하면 두 점 사이 거리 · Δx · Δy), 센서 연결 체크(끄면
  unsubscribe, 켜고 1 s 안에 메시지가 없으면 `데이터 없음`), 바퀴 목표/실측 그래프, 오도메트리, IMU, LiDAR 극좌표,
  조종(↑↓←→/WASD 누르는 동안 이동 · K/스페이스 정지 · 최대 속도 · 1 m 직진/후진 시험 · 기록 · 비상 정지).
- **데이터 점검** — 모든 토픽의 주기 · 지연(수신 시각 − header.stamp) · 상태, 기록할 토픽 체크, CSV 기록,
  직진 시험 결과(목표 직선 대 실제 궤적, 옆 오차 · 방향 오차 · 소요), 이벤트.
- **연결** — 주소 · 포트, 저장된 로봇(Wi-Fi .201 / 유선 .200 + 연결했던 주소), 네트워크에서 :9090 찾기, 연결 단계 확인.
- 3D 조작(Fusion 360 방식): 휠 확대 · 가운데 버튼 끌기 이동 · Shift+가운데 끌기 회전. 가운데 버튼이 없으면 왼쪽 끌기
  (또는 Shift+왼쪽) 회전 · Ctrl+왼쪽 이동 · Alt+왼쪽 확대. 추적 · 위에서 시점에서 조작하면 그 각도 그대로 자유 시점이 된다.
  오른쪽 위 뷰 큐브가 지금 각도를 보여 주고, 면을 누르면 그 뷰로, 오른쪽 클릭하면 홈 · 위 · 아래 · 앞 · 뒤 · 왼쪽 · 오른쪽 뷰 메뉴
  (앞 = odom +x).
- **이동 목표 (Nav2)** — `이동 목표` 를 켜고 바닥에서 목표 위치를 누른 채 바라볼 방향으로 끌고 놓으면(RViz 2D Goal Pose 처럼)
  `/goal_pose`(geometry_msgs/PoseStamped, map→odom TF 가 있으면 map 프레임)로 보내고 모드를 `자율주행` 으로 바꾼다
  (Nav2 속도도 `/teleop/cmd_vel` 로 나와 키보드 조종의 0 속도와 겹치지 않게). `정지` · 비상 정지는
  `/navigate_to_pose/_action/cancel_goal` 로 Nav2 목표를 모두 취소한다.
  결과는 PC 의 `tooling/nav/nav_goal_bridge.py` 가 `/nav/status`(JSON)로 알린다 — 목표를 보낸 뒤 자율주행 중에만
  500 ms throttle 로 구독해 3D 뷰에 계획 중 · 이동 중(남은 거리 · 예상 시간) · 도착 · 실패 · 취소를 보이고 이벤트 로그에 남긴다.
  실패하면 위쪽에 빨간 배너(이유 + 할 일)와 `지도 초기화` 버튼이 뜬다.
- 카메라 · 표시 레이어 · 창 크기는 다음 실행에도 그대로다(QSettings, `HKCU\Software\RobotDashboard`).
- 연결이나 `/link/heartbeat` 가 3 s 넘게 끊기면 테두리가 빨갛고 3D 가 회색으로 멈춘다. 3 s 마다 다시 연결한다.

조종은 `/teleop/cmd_vel` 로 10 Hz(0 포함)를 보내고 파이의 `cmd_watchdog` 이 `/cmd_vel` 로 넘긴다 — 앱이 죽거나
링크가 끊기면 0.5 s 안에 선다. `자율주행` · `정지` 모드에서는 조종 명령을 보내지 않는다(Nav2 · 직진 노드가 `/cmd_vel` 을 쓴다).

## 기록

`records/<날짜_시각>/<토픽>.csv` — 토픽마다 파일 하나, 첫 열 `pc_time`, 나머지는 메시지 필드를 펼친 열
(`pose.pose.position.x` …, 숫자 배열은 공백으로 이은 한 칸).

## Fusion 360 모델 넣기

`dashboard/model/` 에 아래 이름으로 두면 기본 도형 대신 읽는다(.glb/.gltf/.fbx/.obj, Qt RuntimeLoader · assimp).

| 파일 | 내용 | 원점 |
| --- | --- | --- |
| `body.*` | 몸체 전체(바퀴 제외) | base_link (두 바퀴 차축 중심 바닥) |
| `wheel_left.*` · `wheel_right.*` | 바퀴 하나씩 | 차축 중심 — 실측 바퀴 속도로 회전한다 |
| `lidar_head.*` | LiDAR 의 도는 부분 | 회전축 위 — `/scan` 이 들어오는 동안 `scan_time` 의 회전수로 돈다 |
| `parts.json` | LiDAR 헤드 위치(`lidar_head_position_base_link_m`) | — LiDAR 점도 이 높이에 그린다 |

지금 `model/` 의 파일은 Fusion 360 "MobileRobot" 설계를 `tooling/fusion/prepare_dashboard_model.py` 로 바꾼 것이다
(메시는 ~110 MB 라 git 밖, `.worktreeinclude`). 바퀴는 CAD 간격(0.3135 m)이 아니라 펌웨어 실측값 0.34 m 에 놓는다 —
이때 바퀴 안쪽 면(0.144 m)이 몸체 바깥(0.1437 m)과 맞닿아 틈 · 겹침이 없다.

Fusion 360 에서 컴포넌트별로 *내보내기 → FBX 또는 OBJ*(재질 포함)로 내보낸다. 기본은 glTF 규약(Y 위 · m)으로 보고
x 축 90° 회전을 준다. 축 · 단위가 다르면 `qml/RobotModel.qml` 의 `modelRotation` · `modelScale` 을 고친 뒤 다시 빌드한다
(cm 로 내보냈으면 `0.01`). 차체 치수(바퀴 반지름 0.05 m · 간격 0.34 m)는 `firmware/pwm-pico/include/config.h` 와 같다.
