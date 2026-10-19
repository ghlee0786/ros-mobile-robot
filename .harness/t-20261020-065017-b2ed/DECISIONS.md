## 1. B19 오래된 exe 를 경고로 둘지 실패로 둘지
- options: 경고만 출력하고 실행 / 종료 코드 2 로 실행하지 않음 / 옵션으로 고르게
- chosen: `--exe`(기본 dashboard/build/robot_dashboard.exe)가 옆 src·qml 의 가장 새 파일보다 오래됐으면 종료 코드 2
- why: 오래된 exe 결과(시각 정렬·estop 판정)를 믿고 오판한 것이 B19 의 원인이라 경고는 놓치기 쉽다. integration.cmd 는 빌드 뒤에 돌아 영향 없다.
- effect: 소스만 고치고 빌드하지 않으면 실행기가 멈춘다. 경고로 바꾸려면 run_scenarios.py main() 의 `return 2` 를 print 만 남기면 된다. `--dashboard-cmd` · `--no-dashboard` 에는 적용하지 않는다.

## 2. B32 재생 throttle 을 어떻게 맞출지
- options: 재생에서 throttle 무시 / 재생 루프의 flush 를 더 촘촘히 / 다음 송신 시각 기반(GCRA)에 재생 출처만 한 주기 여유
- chosen: 다음 송신 시각(next_due) 기반으로 바꾸고, 재생 출처(Replay.throttle_slack=1.0)는 한 주기 일찍 보내도 되게
- why: 기록은 파이에서 이미 throttle 된 도착 시각이라 간격이 흔들린다. 여유 한 주기는 흔들림만 흡수하고 평균 빈도는 여전히 1/throttle 이하다. 시뮬레이션 모드 동작(최소 간격)은 그대로다.
- effect: 재생에서 두 메시지가 throttle 보다 짧은 간격으로 갈 수 있다. 되돌리려면 replay.py 의 throttle_slack 을 0 으로.

## 3. B25 · B39 · B6 를 시나리오로 판정할 방법
- options: testhooks 에 새 덤프 추가 / InspectorPage 의 시험 훅 요약 속성 + actions 의 wait / 캡처만
- chosen: InspectorPage 요약 속성(topicOkShownAs · diagDetailCount · paramRows)과 ConnectPage Check 의 objectName + `waiting` 속성(wait 대상)
- why: testhooks.cpp 는 이 작업의 파일 범위 밖이고, InspectorPage 는 이미 시험 요약 속성을 두는 곳이다.
- effect: 레이아웃 결함(B36·B38·B40)은 자동 판정 대신 `fixc_inspector_layout` 캡처를 눈으로 본다(상태는 오류 없음·빈 선택만 판정).

## 4. B16 범위
- options: 실기 차이 전부(/diagnostics 없음, /scan_compact, rosbridge 오류 동작, CPU 포화, stamp 차)를 새 옵션으로 / describe 버그만
- chosen: `--foxy-param-bug` 만 새로 더함. /diagnostics 없음은 기존 `--hide-topic /diagnostics --drop /diagnostics:1`, stamp 차는 기존 `--stamp-skew-ms -130` 으로 된다. /scan_compact 는 INT2 가 sim.py 에 더한다.
- why: sim.py 는 INT2 와 겹치고, 목표가 B6 시험에 필요한 describe 흉내를 콕 집어 요구했다.
- effect: rosbridge 오류 동작 · CPU 포화 흉내는 아직 없다. 필요하면 별도 작업으로.
