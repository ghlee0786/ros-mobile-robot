# 기여 안내 · Contributing

로봇이 없어도 기여할 수 있습니다. 대시보드 · 시험 · 내비 도구는 **모의 rosbridge 서버**(`tooling/mockbridge`) 위에서
개발하고 검증하도록 만들어 두었습니다.
You don't need the robot to contribute — the dashboard, tests and nav tools are built to be developed against the
**mock rosbridge server** in `tooling/mockbridge`.

## 1. 개발 환경 · Setup (Windows 11)

| 무엇 · What | 어떻게 · How |
|---|---|
| Qt 6.8.3 MinGW · CMake · Ninja | `tooling\pc\install_qt.ps1` (기본 `E:\Qt`, 다른 곳이면 `set QT_ROOT=…`) |
| Python 3.12 + [uv](https://docs.astral.sh/uv/) | 시험 도구는 `uv run --no-project --with …` 로 의존성을 그때그때 받습니다 |
| (선택) WSL Ubuntu 24.04 + ROS 2 Jazzy | SLAM · Nav2 를 돌릴 때만 — `tooling\wsl\install_wsl_ros.ps1` |

```bat
dashboard\build.cmd            :: 빌드 · build
dashboard\test.cmd             :: 빌드 + 단위 시험(ctest) · build + unit tests
checks\integration.cmd         :: 빌드 + 단위 + smoke + 모의 서버 pytest + 시나리오 전체 · everything, no robot needed
```

## 2. 로봇 없이 대시보드 띄우기 · Run the dashboard without a robot

```bat
set UV_CACHE_DIR=E:\Temp\uv-cache
uv run --no-project --with "websockets>=12,<14" python tooling\mockbridge\mock_rosbridge.py --port 9091 --nav --map-odom 0,0,0
dashboard\run_test.cmd --url ws://127.0.0.1:9091
```

모의 서버는 차동 구동 로봇 · LiDAR(가상 방) · IMU · 지도 · Nav2 경로 추종 · 워치독 · rosapi 를 흉내 내고, 장애
(`--delay-ms --drop --freeze --halfopen --flood --disconnect-every …`)와 실기 기록 재생(`--replay 기록.jsonl --loop --restamp`)을 넣을 수 있습니다.

## 3. 시험 시나리오 더하기 · Adding a scenario

`tooling/mockbridge/scenarios/<id>.json` 하나가 시나리오 하나입니다: 서버 인자 · 대시보드 동작(`actions`) · 판정(`expect`).
형식은 `python tooling/mockbridge/run_scenarios.py --help` 에 있습니다. 새 기능에는 시나리오를 함께 넣어 주세요 —
리뷰어는 `run_scenarios.py --only <id>` 의 결과 폴더(state.json · 캡처 · 로그)로 확인합니다.

## 4. 지켜 주세요 · Ground rules

- **안전 먼저.** 원격 속도 명령은 항상 `/teleop/cmd_vel`(파이 워치독 경유)로 보냅니다. `/cmd_vel` 에 직접 쓰는 코드는 받지 않습니다.
  실기 주행 시험은 바퀴를 띄우고 사람이 옆에 있을 때만 합니다.
- **파이 rosbridge 는 한 코어가 상한입니다.** 새 구독은 필요한 동안만, 주기가 높은 토픽은 throttle 을 겁니다(`RosBridge` 의 토픽별 하한 표).
- **Jazzy 노드를 로봇 도메인(7)에 붙이지 않습니다.** Foxy 가 Jazzy 발견 정보를 해석하지 못해 rosapi 가 죽습니다 — PC 쪽 노드는 도메인 8 에서
  `split_relay` 를 거칩니다.
- **디자인 토큰.** QML 에 색 · 크기 hex 를 직접 쓰지 말고 `qml/Theme.qml` 과 `qml/components/` 를 씁니다.
- **새 파일은 묶음별 cmake 에.** `dashboard/CMakeLists.txt` 의 목록은 고치지 않고 `dashboard/cmake/<묶음>.cmake` 에 더합니다.
  단위 시험은 `dashboard/tests/tst_<묶음>_*.cpp` 를 만들면 자동으로 등록됩니다.
- 커밋 메시지는 한국어 · 영어 모두 좋습니다. "무엇을 왜" 를 써 주세요.

## 5. 릴리스 만들기 · Making a release

1. `dashboard/CMakeLists.txt` 의 `project(robot_dashboard VERSION x.y.z …)` 를 올리고 `CHANGELOG.md` 에 `## [x.y.z] - YYYY-MM-DD` 항목을 써서 커밋합니다.
2. `uv run --no-project python tooling/release/release.py x.y.z` — 확인 후 `dist\RobotDashboard-Setup-x.y.z.exe` 를 만들고 올릴 내용을 보여 줍니다(dry run).
   설치 파일 시험은 `checks\installer.cmd`(무인 설치 · 업그레이드 · 제거).
3. `… release.py x.y.z --publish` — 태그 `vx.y.z` 를 만들고(push 명령을 알려 줌) GitHub 릴리스(본문 = CHANGELOG 항목)와 설치 파일을 올립니다.
   토큰은 `git credential fill` 로 얻습니다. 설치된 대시보드는 이 릴리스를 보고 업데이트를 알립니다.

## 6. 처음 해 보기 좋은 일 · Good first issues

- 대시보드 패널 레이아웃 저장 · 불러오기
- 카메라(CompressedImage) 패널
- MCAP 기록과 타임라인 재생
- 웨이포인트 여러 개를 이어 보내는 미션 패널
- 영어 UI 문자열(번역 파일)
- 펌웨어: 명령 타임아웃 · PID 피드포워드(`firmware/pwm-pico`)

질문은 [Discussions](https://github.com/ghlee0786/ros-mobile-robot/discussions) 에 남겨 주세요.
