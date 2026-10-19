# ROS 모바일 로봇 워크스페이스

라즈베리 파이 4 + Raspberry Pi Pico 로 움직이는 차동 구동 로봇을, Windows PC 에서 원격으로 보고 · 조종하고 ·
지도 위 목표로 보내기 위한 워크스페이스입니다. 로봇 쪽 ROS 2 코드, Pico 펌웨어(서브모듈), 그리고 PC 와 로봇을
잇는 인프라 · 3D 대시보드 · SLAM/Nav2 구성이 들어 있습니다.

![구성](https://img.shields.io/badge/robot-ROS%202%20Foxy-22314E) ![PC](https://img.shields.io/badge/PC-ROS%202%20Jazzy%20(WSL)-22314E) ![Qt](https://img.shields.io/badge/dashboard-Qt%206.8%20Quick%203D-41CD52)

## 시스템 구성

```
 ┌──────────── 로봇 (Raspberry Pi 4 · Ubuntu 20.04 · ROS 2 Foxy) ────────────┐
 │  RPLIDAR A3 ── /scan        CMP10A IMU ── /imu/data_raw                   │
 │  Pico (PID · 엔코더 오도메트리) ⇄ USB 시리얼 ⇄ control_node ── /odom, /wheel_states │
 │  robot_link 서비스: rosbridge :9090 · cmd_watchdog · odom→base_link TF ·  │
 │                     직진 제어 · 핑퐁                                       │
 └───────────────▲─────────────────────────────────────▲─────────────────────┘
        Wi-Fi (같은 공유기) │ rosbridge (WebSocket JSON)        │ ROS 2 DDS (도메인 7, CycloneDDS)
 ┌───────────────┴──────────── PC (Windows 11) ───────┴─────────────────────┐
 │  dashboard/  Qt 6 Quick 3D 앱 — 3D 모델 · 센서 · 그래프 · 조종 · Nav2 목표  │
 │  WSL Ubuntu 24.04 · ROS 2 Jazzy:                                          │
 │    split_relay (도메인 7 ⇄ 8) ── SLAM(slam_toolbox) · Nav2 · RViz (도메인 8, FastDDS) │
 └───────────────────────────────────────────────────────────────────────────┘
```

- **로봇 코드는 파이의 `~/robot_ws` 가 기준**입니다(`integrated_robot_control` 은 GitHub 에 없는 로컬 브랜치 `ver1.3`).
  이 저장소의 `ros_ws/src/*` 서브모듈은 GitHub 쪽 사본입니다.
- 모든 원격 속도 명령(대시보드 · 키보드 조종 · Nav2)은 `/teleop/cmd_vel` 로 가고, 파이의 `cmd_watchdog` 이
  `/cmd_vel` 로 넘깁니다. PC 나 Wi-Fi 가 끊기면 **0.5 초 안에 로봇이 멈춥니다.**

## 하드웨어

| 부품 | 내용 |
|---|---|
| 컴퓨터 | Raspberry Pi 4 Model B (4 GB) · Ubuntu 20.04.6 · ROS 2 Foxy |
| 모터 제어 | Raspberry Pi Pico (RP2040) · MD10C R3 × 2 · 24 V 엔코더 모터 × 2 · 펌웨어 `firmware/pwm-pico` |
| 센서 | RPLIDAR A3 (`/dev/rplidar`), CMP10A IMU (`/dev/imu_usb`) |
| 치수 | 바퀴 반지름 0.05 m, 바퀴 간격 0.34 m(실측 · 펌웨어 설정), 속도 한계 0.1 m/s · 0.25 rad/s |

## 저장소 구조

| 경로 | 내용 |
|---|---|
| `dashboard/` | Qt 6 C++ 대시보드 앱 (자세한 내용은 `dashboard/README.md`) |
| `tooling/rpi/` | 파이 네트워크 설정 · `robot_link` 서비스 · 배포 스크립트 · 로봇 코드 패치 |
| `tooling/wsl/` | WSL(ROS 2 Jazzy) 설치 · DDS 환경 · 통신 점검 · RViz · 키보드 조종 |
| `tooling/nav/` | SLAM / Nav2 실행 · 파라미터 · 도메인 중계 · 지도 저장 · 목표 이동 |
| `tooling/pc/` | Windows 쪽 설정(관리자) · 실행용 `.cmd` · rosbridge 도구 · Qt 설치 |
| `tooling/fusion/` | Fusion 360 설계 → 대시보드 3D 모델 변환 |
| `tooling/sim/` | 하드웨어 없이 직진 제어 · 워치독을 시험하는 시뮬레이터 |
| `ros_ws/src/` | 로봇 ROS 2 패키지(서브모듈) |
| `firmware/pwm-pico/` | Pico 모터 제어 펌웨어(서브모듈) |
| `harness/` | 개발 하네스(작업 격리 · git 안전 · 검사) |

## 빠른 시작

### 1. 로봇(파이) — 한 번만

```bash
# PC 에서. 파이는 ipTIME 공유기에 연결, 비밀번호 로그인 후 키 등록이 끝난 상태여야 한다.
WIFI_SSID=<와이파이> WIFI_PASSWORD=<비밀번호> \
  ssh rpi 'sudo -E bash -s' < tooling/rpi/apply_network.sh     # 유선 .200 · 무선 .201 고정(실패 시 4분 뒤 자동 복구)
tooling/rpi/deploy_robot_link.sh rpi                            # 서비스 설치 · 부팅 시 자동 실행
tooling/rpi/patches/add_wheel_states.sh rpi                     # /wheel_states · /wheel_cmd 발행
tooling/rpi/patches/fix_wit_imu_busy_loop.sh rpi                # IMU 드라이버 CPU 100% → 22%
```

이후로는 파이 전원을 켜기만 하면 Wi-Fi(192.168.1.201)로 붙고 서비스 6개가 자동으로 올라옵니다:
`robot-bringup`(LiDAR · IMU · Pico) · `robot-rosbridge`(:9090) · `robot-cmd-watchdog` · `robot-odom-tf` ·
`robot-straight-drive` · `robot-pingpong`. 유선을 꽂거나 빼면 DDS 인터페이스를 바꾸려고 자동으로 재시작합니다.

### 2. PC — 한 번만

- 관리자 PowerShell: `tooling/pc/setup_pc_network_admin.ps1`(파이로 가는 경로 · WSL 방화벽),
  `tooling/pc/fix_time_sync_admin.ps1`(시간 동기화 — PC 시계가 틀리면 TF 가 깨진다)
- `%USERPROFILE%\.wslconfig`: `networkingMode=mirrored`, `[experimental] hostAddressLoopback=true`
- WSL 배포판: `tooling/wsl/install_wsl_ros.ps1`(Ubuntu-ROS · ROS 2 Jazzy · Gazebo) →
  `tooling/wsl/install_nav_stack.sh`(slam_toolbox · Nav2 · laser_filters · DDS 버퍼)
- Qt: `tooling/pc/install_qt.ps1`(Qt 6.8.3 MinGW · CMake · Ninja → `E:\Qt`)

### 3. 사용

| 하고 싶은 것 | 실행 |
|---|---|
| 대시보드(3D · 센서 · 그래프 · 조종 · Nav2 목표) | `dashboard\run.cmd` |
| 지도 만들기(SLAM + Nav2 + RViz) | `tooling\pc\map_start.cmd` → 끝나면 `tooling\pc\map_save.cmd` |
| 저장한 지도로 이동 | `tooling\pc\nav_start.cmd` → RViz/대시보드에서 위치 지정 후 목표 클릭 |
| 터미널 키보드 조종 | `tooling\pc\teleop.cmd` |
| 1 m 보정 직진 | 대시보드 버튼, 또는 `uv run --no-project --with websocket-client python tooling/pc/straight_drive.py 1.0 0.08` |
| 통신 점검 | `wsl -d Ubuntu-ROS -- bash tooling/wsl/run_dds_test.sh` · `python tooling/pc/rosbridge_pingpong_test.py` |

## 대시보드

Qt 6 Quick 3D 로 만든 Windows 데스크톱 앱입니다. 파이의 rosbridge 에만 붙어서 ROS 버전과 상관없이 동작합니다.

- Fusion 360 설계에서 내보낸 실제 로봇 모델 — 바퀴는 `/wheel_states` 로, LiDAR 헤드는 스캔 주기로 회전
- 3D 뷰: LiDAR 점 · 이동 경로 · 계획 경로 · 지도 · 좌표축, 추적 / 위에서 / 자유 시점, 뷰 큐브(우클릭으로 위 · 아래 ·
  좌 · 우 · 앞 · 뒤, 직교/원근), Fusion 360 식 마우스 조작(휠 확대 · 가운데 이동 · Shift 회전 · Ctrl/Alt 조합), 거리 측정
- **이동 목표(Nav2)**: 누른 곳 = 위치, 끄는 쪽 = 도착 방향, 놓으면 `/goal_pose` 전송(RViz 2D Goal Pose 방식)
- 센서 연결 패널: 센서마다 구독 켜기/끄기와 `수신 중 / 데이터 없음 / 꺼짐` 판정
- 바퀴 목표/실측 · IMU · 오도메트리 그래프, 키보드 조종, 1 m 직진 시험, CSV 기록, 비상 정지, 끊김 경고

## 통신 설계에서 정한 것

- **대시보드 ↔ 로봇: rosbridge.** 파이의 rosbridge(Python)는 한 코어가 상한이라 IMU 는 20 Hz, TF 는 5 Hz 로
  받아 표시합니다(오도메트리 · 바퀴 36 Hz, LiDAR 12 Hz 는 전부).
- **SLAM/Nav2 ↔ 로봇: 도메인 분리.** Jazzy 노드 ~20개가 Foxy 로봇과 직접 DDS 로 붙으면 불안정했습니다.
  내비 스택은 PC 내부 도메인 8(FastDDS)에서 돌고, `tooling/nav/split_relay.py` 가 도메인마다 단일 RMW 프로세스로
  토픽을 중계합니다(로봇→PC `/scan /odom /tf /tf_static /goal_pose`, PC→로봇 `/teleop/cmd_vel /map /plan`).
- **로봇 자기 몸체 제거.** RPLIDAR 가 기둥 · 프레임을 0.14 ~ 0.19 m 에서 보므로 SLAM 입력과 코스트맵에서
  0.3 m 안쪽 점을 지웁니다.

## 직진 주행

Pico 의 바퀴 PID 가 사실상 P 제어라 좌우 모터 차이가 그대로 남아 로봇이 휩니다. `robot_link/straight_drive_node.py`
가 IMU yaw 와 엔코더 방향을 섞은 방향 오차와 출발선 대비 옆 오차로 각속도를 보정합니다. 좌우 바퀴 95% · 93% 차이를
흉내 낸 시뮬레이터에서 2 m 주행 시 옆 오차가 28 ~ 39 cm → 1 cm 안팎으로 줄었습니다(`tooling/sim`).

## 안전

- 로봇을 움직이는 시험은 사람이 옆에 있을 때만 합니다. 처음에는 바퀴를 바닥에서 띄우세요.
- Pico 펌웨어와 `control_node` 에는 명령 타임아웃이 없습니다 — 원격 명령은 반드시 `/teleop/cmd_vel`(워치독)로 보냅니다.
- 끝낼 때는 정지 명령 후 `sudo shutdown -h now`.

## 알려진 문제 · 다음 할 일

- 펌웨어: 명령 타임아웃(워치독), 바퀴 PID(피드포워드 · 적분), 속도 한계 상향
- CAD 의 바퀴 간격(313.5 mm)과 실측 설정(340 mm)이 다릅니다 — 대시보드는 설정값을 따릅니다
- 파이 Foxy 는 지원 종료(EOL) 판입니다. Foxy 노드는 Jazzy 의 노드 정보(`ros_discovery_info`)를 해석하지 못해 로그를 많이 남깁니다
- 2차: 지도 기록 재생(타임라인), 점군 · 카메라
