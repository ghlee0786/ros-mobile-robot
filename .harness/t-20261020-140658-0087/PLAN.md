# PLAN — 인스톨러 · 자동 업데이트 알림 · 릴리스 도구

## 목표
dashboard(robot_dashboard.exe, Qt 6.8.3 MinGW)를 Inno Setup 설치 파일로 배포하고(제품 특징 · 기본 설정 · 업그레이드 시 설정 유지),
GitHub Releases 를 조회해 새 버전 알림 · 내려받아 설치까지 하며, 릴리스를 만드는 도구를 둔다. 검사: `checks\installer.cmd`.

## 진행
- [x] 버전: `dashboard/CMakeLists.txt` project(VERSION 0.1.0) → `cmake/update.cmake` 의 ROBOT_DASHBOARD_VERSION → main.cpp setApplicationVersion. 명령 팔레트 '정보 · 로봇 대시보드 v0.1.0'
- [x] CHANGELOG.md (Keep a Changelog, [0.1.0] - 2026-10-20)
- [x] `dashboard/src/update/updater.{h,cpp}` (QML_ELEMENT Updater): 10 s 뒤 · 6 h 마다 조회, ROBOT_DASHBOARD_UPDATE_URL, semver 비교, skipVersion · updateCheck 설정, 내려받기(크기 확인) → /SILENT … /RELAUNCH=1 실행 후 종료, 주행 중 차단
- [x] `qml/components/UpdateBanner.qml` + Main.qml(Updater · 배너) + CommandPalette('업데이트 확인' · 자동 확인 켜기/끄기 · 정보)
- [x] 기록 위치: 설치본(exe 옆 model/)은 model=exe 옆, records=문서/RobotDashboard/records (robot.cpp dataDir/recordsDir/installed)
- [x] 단위 시험 `tests/tst_update_updater.cpp` (semver · JSON · QTcpServer HTTP 200 새/같은/옛 · 404 · 403 · 깨진 JSON · 건너뛰기 · 주행 중 차단)
- [x] `installer/robot_dashboard.iss` · `installer/assets/*.bmp`(make_assets.py, Pillow) · `installer/build_installer.cmd`
- [x] `installer/test_installer.py`: 무인 설치 → 설치본 실행 → 업그레이드(설정 유지 · 표식 파일 삭제) → 무인 제거(설정 남음) → 정리
- [x] `tooling/release/release.py` (dry run 만 시험, --publish 실행 안 함)
- [x] README '💾 설치' 절, CONTRIBUTING '릴리스 만들기', .gitignore dist/
- [x] `checks\installer.cmd` 전체 통과 확인 (ctest 20/20 · 인스톨러 시험 통과)

## 발견
- .cmd 는 CRLF 여야 한다(LF 면 for/괄호 파싱이 깨진다). .gitattributes 가 *.cmd 를 crlf 로 둔다.
- 하네스 가드: 내 셸 명령에서 worktree 밖(E:\Temp 등) 파일 쓰기 금지 → 시험 산출물은 dashboard/build 에. test_installer.py 자체는 E:\Temp\rdinstall_test 를 쓴다(명세).
- 제거 프로그램(unins000.exe)은 임시 폴더로 자기를 복사해 돌고 바로 끝난다 → 파일이 사라질 때까지 기다린다.
- Inno 의 Restart Manager 재시작(RestartApplications)은 앱이 RegisterApplicationRestart 를 해야 동작 → 자동 업데이트 뒤 재실행은 /RELAUNCH=1 로 한다.

## 남은 일
- 없음(검사 통과 확인 후 끝). 실제 GitHub 릴리스(--publish)는 사용자가 직접 한다.
