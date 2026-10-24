# DECISIONS

## 1. 기본 설치 범위(사용자별 vs 모든 사용자)
- options: PrivilegesRequired=admin(기본 모든 사용자, UAC) · PrivilegesRequired=lowest(기본 사용자별, 대화상자에서 모든 사용자 선택 가능)
- chosen: lowest + PrivilegesRequiredOverridesAllowed=dialog
- why: 자동 업데이트의 무인 설치가 UAC 창 없이 끝나고, 설정(HKCU)과 같은 사용자 범위라 단순하다. {autopf} 는 선택에 따라 Program Files 또는 %LOCALAPPDATA%\Programs 가 된다.
- effect: 첫 설치 대화상자의 기본 선택이 '나만 설치'. 바꾸려면 installer/robot_dashboard.iss 의 PrivilegesRequired=admin.

## 2. 업그레이드 때 이전 파일 지우기 방식
- options: [InstallDelete] `{app}\*` 전체(제거 프로그램 unins000.* 까지 지워짐) · 프로그램 파일 목록(exe · *.dll · qml · model · 플러그인 폴더)만 지움 · [Code] 로 unins 를 빼고 모두 지움
- chosen: [InstallDelete] 에 프로그램 파일 · 폴더 목록을 적는다
- why: 제거 정보(unins000.dat)를 지우면 업그레이드 뒤 제거가 깨질 수 있다. windeployqt 가 만드는 폴더 이름은 정해져 있다.
- effect: 새 Qt 플러그인 폴더가 생기면 iss 의 [InstallDelete] 목록에 한 줄 더한다(남아도 다음 제거 때 지워진다).

## 3. 시험이 사용자 설정을 건드리지 않게 하는 방법
- options: 실제 키(HKCU\Software\RobotDashboard\RobotDashboard)에 시험 값 이름만 따로 씀 · 설치 파일에 숨은 인자 /SETTINGSKEY 를 두어 시험 때 다른 키를 씀
- chosen: /SETTINGSKEY=Software\RobotDashboardInstallerTest\RobotDashboard (기본값은 실제 키)
- why: 첫 설치가 비어 있는 host · port 를 기본값으로 쓰는 동작까지 사용자 설정을 건드리지 않고 시험할 수 있다. 끝나면 시험 키 전체를 지운다.
- effect: 일반 사용자는 인자를 모르므로 영향 없다. 없애려면 iss 의 SettingsKey 함수를 상수로 바꾼다.

## 4. 자동 업데이트 설치 뒤 다시 켜기
- options: RestartApplications 에만 기대기 · [Run] 항목을 무인 설치에서도 실행 · 숨은 인자 /RELAUNCH=1 일 때만 실행
- chosen: Updater 가 /SILENT … /RELAUNCH=1 을 넘기고, iss 의 WantRun 이 무인 모드에서는 이 인자를 볼 때만 실행
- why: Restart Manager 재시작은 앱이 RegisterApplicationRestart 를 해야 동작하고, 무조건 실행하면 시험용 /VERYSILENT 설치에서도 앱이 뜬다.
- effect: 명세의 인자 목록에 /RELAUNCH=1 하나가 더해졌다. updater.cpp 의 args 에서 빼면 설치 뒤 다시 켜지지 않는다.

## 5. pre-release 버전 비교 규칙
- options: semver 2.0 그대로(0.2.0-rc1 < 0.2.0) · '-' · '+' 뒤를 무시하고 숫자만 비교
- chosen: 무시하고 x.y.z 숫자만 비교(v 접두 허용, 빠진 자리는 0)
- why: releases/latest 는 pre-release 를 돌려주지 않아 실제로 만날 일이 거의 없고, 규칙이 단순하다.
- effect: v0.2.0-rc1 과 0.2.0 은 같은 버전으로 본다. Updater::compareVersions 한 곳만 고치면 된다.

## 6. 앱 안에서 '업데이트 확인 끔' 을 바꾸는 곳
- options: 설치 마법사에서만 · 명령 팔레트에도 켜기/끄기 명령
- chosen: 명령 팔레트에 '시작할 때 업데이트 확인 켜기/끄기' 를 더함(같은 updateCheck 설정)
- why: 설치 뒤 설정을 바꾸려고 다시 설치해야 하는 것은 불편하다. 끔이어도 '업데이트 확인' 명령(수동)은 동작한다.
- effect: CommandPalette.qml 의 두 줄. 수동 확인도 막으려면 Updater::check 에서 checkEnabled 를 본다.

## 7. 시험 모드에서 자동 조회
- options: 시험 모드에서도 GitHub 조회 · ROBOT_DASHBOARD_TEST=1 이면 ROBOT_DASHBOARD_UPDATE_URL 을 줄 때만 조회
- chosen: 후자
- why: 시나리오 · 인스톨러 시험이 인터넷과 GitHub rate limit 에 따라 달라지지 않게.
- effect: Updater 생성자의 조건 한 줄.

## 8. release.py 의 dry run 범위
- options: dry run 은 아무것도 하지 않고 보여 주기만 · 확인과 설치 파일 빌드는 하고 GitHub 에 올리는 일만 보여 줌
- chosen: 후자(빌드는 늘 하고 태그 · 릴리스 · asset 만 --publish 에서)
- why: 명세의 '하는 일' 순서가 확인 → 빌드 → (--publish 일 때만) 올리기 이고, 올리기 전에 설치 파일을 시험할 수 있다.
- effect: release.py main() 의 순서. 빌드도 건너뛰려면 dry run 분기를 빌드 앞으로 옮긴다.

## 9. 이름 변경 뒤 문서 폴더의 기록 위치 이름
- options: 문서\RobotDashboard\records 유지 · 문서\ROS Monitor\records 로 바꿈
- chosen: 유지
- why: 추가 요구의 바꿀 목록에 없고, 설정 키처럼 내부 식별자(RobotDashboard)를 그대로 두는 쪽과 맞다. 기존 기록을 옮길 일이 없다.
- effect: Robot::recordsPath 의 "/RobotDashboard" 한 곳만 바꾸면 된다(test_installer.py 의 기대 경로도 함께).

## 10. --no-publish 에서 가감속 파라미터 조회(tst_arm 실패) 고치는 쪽
- options: 시험을 고쳐 get_parameters 를 허용 · 앱에서 noPublish 면 조회를 건너뜀
- chosen: 앱에서 건너뜀(시험 기준 유지)
- why: B10 은 --no-publish 에서 rosapi 밖 서비스를 부르지 않는 것이 기준이고, 시험은 바꾸지 말라는 지시다. 조회를 건너뛰면 rampKnown 이 false 로 남을 뿐 안전 동작은 같다.
- effect: robot.cpp updateStatus 의 조건 하나.

## 11. 설치본이 개발 배치와 같게 도는지 확인하는 방법
- options: 파일 존재만 확인 · 앱이 실제로 읽은 결과를 state.json 에 내보내 확인
- chosen: state.json 에 deploy 절(글꼴 · SVG · TLS 백엔드 · 데이터/기록 폴더)과 scene.modelLoad(RuntimeLoader 결과)를 더함
- why: 플러그인 · 글꼴 · glb 는 파일이 있어도 읽기에 실패할 수 있다. 같은 값을 개발 배치에서도 볼 수 있다.
- effect: testhooks.cpp 의 snapshot 과 RobotModel/SceneView 의 읽기 전용 속성 한 개씩.

## 12. 앱 화면의 이름
- options: 'ROS 모니터' 만 · 'ROS 모니터(로봇 대시보드)'
- chosen: 창 제목 · 머리줄 · 팔레트는 'ROS 모니터'. --help 설명 첫 줄만 'ROS 모니터(로봇 대시보드)' 로 옛 이름을 함께 둔다(exe 이름이 robot_dashboard 라서).
- why: 사용자 요구가 화면 제목 변경이다.
- effect: Main.qml 두 곳 · main.cpp 한 곳.
