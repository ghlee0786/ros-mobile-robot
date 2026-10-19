# P2 대시보드 명령줄 시험 모드

## 목표
robot_dashboard.exe 에 명령줄 시험 모드(--url · --settings · --no-autoconnect · --page · --size · --actions · --screenshot ·
--state · --quit-after · --log · --fail-on-warning · --trace/--trace-hz · --data-dir · --no-model · --no-publish)를 넣고,
checks\p2_testhooks.cmd(빌드 + dashboard/tests_hooks/run_hooks_test.py)를 통과시킨다.

## 진행
- [x] src/appconfig.{h,cpp}: AppConfig 싱글턴(host · port · autoConnect · page · testMode)
- [x] src/testhooks.{h,cpp}: 로그 핸들러, actions(call · set · shot · key · wait), 캡처, state(proc · render · actions_done 포함), trace
- [x] src/main.cpp: QCommandLineParser, --help 에 사용법
- [x] cmake/testhooks.cmake(target_sources + psapi) — target_sources 로 더한 QML_SINGLETON 이 등록됨을 확인(CMakeLists 는 안 고침)
- [x] qml/Main.qml: Component.onCompleted · Timer 줄만
- [x] run_test.cmd(배치 안에서 GUI exe 를 기다림 — 종료 코드 2 · 3 이 그대로 돌아오는 것으로 확인, start 불필요)
- [x] tests_hooks/run_hooks_test.py: 가짜 rosbridge(port 0), 6 가지 확인
- [x] checks\p2_testhooks.cmd 통과

## 발견
- AppConfig 에 공개 기본 생성자가 있으면 QML 이 create() 대신 새 객체를 만든다 → 생성자를 private 로.
- 조직 이름마다 Qt 셰이더 캐시(C:\Users\...\AppData\Local\<조직>)가 따로 생겨 첫 실행이 10 s 넘게 늦다 → --settings 에서도 조직 이름은 RobotDashboardTest 로 고정.
  시행 중 만든 C:\Users\Sensorway_GH\AppData\Local\cfg · x 캐시 폴더가 남아 있다(worktree 밖이라 지우지 않음).

## 남은 일
없음.
