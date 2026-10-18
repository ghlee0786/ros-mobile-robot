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

- **대시보드** — 3D 뷰(카메라 추적 · 위에서 · 자유, 레이어 LiDAR 점 · 이동 경로 · 좌표축 · 지도), 센서 연결 체크(끄면
  unsubscribe, 켜고 1 s 안에 메시지가 없으면 `데이터 없음`), 바퀴 목표/실측 그래프, 오도메트리, IMU, LiDAR 극좌표,
  조종(↑↓←→/WASD 누르는 동안 이동 · K/스페이스 정지 · 최대 속도 · 1 m 직진/후진 시험 · 기록 · 비상 정지).
- **데이터 점검** — 모든 토픽의 주기 · 지연(수신 시각 − header.stamp) · 상태, 기록할 토픽 체크, CSV 기록,
  직진 시험 결과(목표 직선 대 실제 궤적, 옆 오차 · 방향 오차 · 소요), 이벤트.
- **연결** — 주소 · 포트, 저장된 로봇(Wi-Fi .201 / 유선 .200 + 연결했던 주소), 네트워크에서 :9090 찾기, 연결 단계 확인.
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

Fusion 360 에서 컴포넌트별로 *내보내기 → FBX 또는 OBJ*(재질 포함)로 내보낸다. 기본은 glTF 규약(Y 위 · m)으로 보고
x 축 90° 회전을 준다. 축 · 단위가 다르면 `qml/RobotModel.qml` 의 `modelRotation` · `modelScale` 을 고친 뒤 다시 빌드한다
(cm 로 내보냈으면 `0.01`). 차체 치수(바퀴 반지름 0.05 m · 간격 0.34 m)는 `firmware/pwm-pico/include/config.h` 와 같다.
