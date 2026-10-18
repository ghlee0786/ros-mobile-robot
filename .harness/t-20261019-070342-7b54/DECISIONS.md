# DECISIONS

## 1. 앱 위치와 빌드 도구
- options: 저장소 루트 `dashboard/` · `tooling/pc/dashboard/` · 별도 저장소 / MSVC · MinGW
- chosen: `dashboard/` (CMake + Qt 6.8.3 MinGW + Ninja, `build.cmd` · `run.cmd`)
- why: tooling/ 은 스크립트 모음이고 이 앱은 독립 C++ 프로젝트다. PC 에 깔린 Qt 가 MinGW 판(E:\Qt\6.8.3\mingw_64)뿐이다.
- effect: 폴더를 옮기려면 디렉터리만 옮기면 된다(경로 의존 없음). MSVC Qt 를 깔면 `build.cmd` 의 PATH 만 바꾼다.

## 2. 로봇과의 연결 방식
- options: rosbridge 웹소켓(JSON) · PC 에 ROS 2 를 깔아 DDS 직접 구독
- chosen: rosbridge (`ws://192.168.1.201:9090`, 이미 Pi 에 robot-rosbridge 서비스가 있음)
- why: 와이어프레임 · 스토리보드가 rosbridge 를 전제로 하고, Windows 네이티브 앱에 ROS 2 가 필요 없으며 DDS-over-WSL 불안정 문제를 피한다.
- effect: `/scan` 처럼 큰 메시지도 JSON 이다(1800 점 12 Hz 에서 문제 없음). 더 빠르게 하려면 rosbridge CBOR 로 바꿀 수 있다(`RosBridge::sendSubscribe` 에 `compression`).

## 3. LiDAR 점 · 경로를 그리는 방법
- options: 점/선 프리미티브 · 작은 큐브 인스턴싱
- chosen: 큐브 인스턴싱(`PointInstancing`)
- why: Windows 기본 D3D11 백엔드는 점 크기 · 선 굵기를 지원하지 않아 1 px 로만 보인다.
- effect: 크기 · 색은 `SceneView.qml` 의 `Dots { size; color }` 로 바꾼다.

## 4. 3D 로봇 모델
- options: Fusion 360 MCP 로 바로 가져오기 · 파일을 `model/` 에 두면 실행 중 로드 · balsam 으로 미리 변환해 빌드에 포함
- chosen: `model/body|wheel_left|wheel_right.(glb|gltf|fbx|obj)` 를 RuntimeLoader 로 로드, 없으면 실제 치수의 기본 도형(PBR 재질)
- why: 아직 Fusion 360 설계 파일이 저장소에 없고 Fusion 360 MCP 도 연결돼 있지 않다. 파일만 넣으면 다시 빌드 없이 바뀐다. 바퀴를 따로 받아야 실측 속도로 돌릴 수 있다.
- effect: 축 · 단위 보정은 `qml/RobotModel.qml` 의 `modelRotation` · `modelScale`. 임시 OBJ 로 로드 경로를 확인했다.

## 5. 키보드 조종 방식
- options: 기존 터미널 teleop 처럼 키를 누를 때마다 속도를 단계적으로 올림 · 누르고 있는 동안만 이동
- chosen: 누르고 있는 동안 최대 속도(슬라이더, 0.02~0.10 m/s) · 회전 0.25 rad/s, 떼면 0
- why: GUI 의 방향 버튼과 같은 동작이고, 손을 떼면 서는 쪽이 안전하다.
- effect: `Robot::drive(linear, angular)` 가 -1..1 비율을 받는다. 단계식이 필요하면 QML 키 처리만 바꾼다.

## 6. 기록 파일 형식
- options: 와이어프레임 예시처럼 CSV 한 파일 · 토픽마다 CSV · MCAP
- chosen: `records/<시각>/<토픽>.csv` (메시지 필드를 펼친 열, 첫 열 pc_time)
- why: 토픽마다 열이 달라 한 파일에 섞으면 분석이 어렵다. MCAP 은 별도 라이브러리가 필요하다.
- effect: `Robot::record` 한 곳에서 바뀐다.

## 7. 검증 중 로봇을 실제로 움직이지 않음
- options: 직진 시험 · 키보드 조종을 실제로 보내 확인 · 수신 · 표시 · 기록만 확인
- chosen: 수신 · 표시 · 기록 · 연결만 실제 Pi 로 확인, 움직이는 명령은 보내지 않음(0 속도 /teleop/cmd_vel 만 나감)
- why: 사람이 옆에 없는 상태에서 로봇을 1 m 움직이는 것은 되돌릴 수 없는 위험이 있다.
- effect: 조종 · 직진 시험 · 비상 정지 · 연결 끊김 화면은 사용자가 로봇 옆에서 처음 시험해야 한다.
