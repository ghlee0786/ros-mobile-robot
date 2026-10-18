# PLAN — 로봇 대시보드 (Qt 6 Quick 3D, C++)

## 목표
첨부 와이어프레임(Main/Inspector/Connect/Storyboard.dc.html)대로, rosbridge(:9090)로 Pi 에 붙어
바퀴 속도 · 오도메트리 · IMU · LiDAR 를 한 화면에 보이고, 로봇 3D 모델(PBR, Fusion 360 내보내기 파일 교체 가능)이
오도메트리대로 움직이는 C++ Qt 6 Quick 3D 데스크톱 앱을 만든다. 센서는 체크로 구독/해제하며 연결 상태를 확인한다.

## 구조
- `dashboard/` — CMake + Qt 6.8.3 (E:\Qt\6.8.3\mingw_64, MinGW 13.1, Ninja)
  - `src/rosbridge.*` : QWebSocket rosbridge 클라이언트(구독 refcount, 재연결 3 s, 핑 지연, 토픽 통계, rosapi 호출, 네트워크 찾기)
  - `src/robot.*` : 토픽 해석(odom/wheel/imu/scan/tf/map/plan/straight), 센서 행·토픽 표, 조종(10 Hz /teleop/cmd_vel), 직진 시험, CSV 기록, 이벤트
  - `src/pointinstancing.*` : LiDAR 점·경로를 작은 큐브 인스턴스로 그림(D3D11 은 점 크기·선 굵기 미지원)
  - `src/maptexture.*` : OccupancyGrid → 텍스처
  - `qml/` : Main(헤더·탭), DashboardPage, SceneView(3D), RobotModel, InspectorPage, ConnectPage, Panel
  - `model/` : Fusion 360 에서 내보낸 body / wheel_left / wheel_right (.glb/.gltf/.fbx/.obj) 를 두면 기본 모델 대신 로드
  - `build.cmd` / `run.cmd`

## 진행
- [x] 첨부 4개 · 기존 tooling · Pi 실제 토픽 확인 (rosbridge 192.168.1.201:9090 수신 확인)
- [x] C++ / QML 작성 (`dashboard/`, README.md 포함)
- [x] 빌드 통과 (`dashboard\build.cmd`)
- [x] 실제 Pi 에 붙여 실행 · 스크린샷 확인 (scratch/dashboard.png · inspector.png · connect.png · final.png)
- [x] CSV 기록 4 s 시험 (odom/imu/wheel 119 줄, scan 49 줄) 후 시험 파일 삭제
- [x] model/body.obj 임시 파일로 RuntimeLoader 교체 확인 후 삭제
- [ ] (사용자) 로봇 옆에서 키보드 조종 · 1 m 직진 시험 · 비상 정지 · 연결 끊김 화면 확인 — 무인 상태라 움직이는 명령은 보내지 않았다
- [ ] (사용자) /map 이 있을 때 지도 레이어 방향 확인 — 시험 중 /map 발행자가 없었다

## 검증 도구
- `scratch/shot.ps1 -Out <png> [-WaitSec n] [-Clicks "x,y,s;w:x,y,s"] [-Height h]` : 앱을 화면 밖(-4000,0)에 띄워 PrintWindow 로 창만 캡처 (사용자 화면을 캡처하지 않음)
- `scratch/probe.py` : rosbridge 토픽 목록 · 표본 확인

## 발견
- Pi 토픽: /odom, /scan(1800점, frame base_link, inf→null), /imu/data_raw, /wheel_states·/wheel_cmd(JointState velocity rad/s, position 비어 있음 → 바퀴 각도는 적분), /tf(odom→base_link), /link/heartbeat(1 Hz), /straight_drive/{goal,cancel,status}, /teleop/cmd_vel(워치독 입력)
- 바퀴 반지름 0.05 m, 간격 0.34 m (firmware/pwm-pico/include/config.h), 펌웨어 한계 0.1 m/s · 0.25 rad/s
- 로봇 몸체 LiDAR 점 0.3 m 이내는 숨김(기존 nav 설정과 같은 기준)

## 남은 일
- Fusion 360 원본/내보내기 파일이 아직 없음 → `dashboard/model/` 에 넣으면 교체됨
