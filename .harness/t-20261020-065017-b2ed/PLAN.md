# FIXC — 점검 화면 · 모의 서버 · 시나리오 실행기 결함 수정

## 목표
B6 B16 B17 B18 B19 B20 B25 B32 B33 B36 B38 B39 B40 B49 를 고치고, 결함마다 재현 시험을 저장소에 남긴다.
검사: `checks\integration.cmd`.

## 진행
- [x] B6 파라미터 행: `ParamClient::merge` — 이름은 list_parameters, 형식·값은 get_parameters, descriptor 는 이름이 맞는 것만.
      시험: `tst_tools_params::foxyDescribeBug`, 시나리오 `fixc_params_foxy`(모의 `--foxy-param-bug`).
- [x] B16 모의 서버 `--foxy-param-bug`(services.py). 시험: `tests/test_pi_contract.py`.
      /diagnostics 없음은 기존 `--hide-topic /diagnostics --drop /diagnostics:1` 로 흉내 낼 수 있다. /scan_compact 는 INT2 몫.
- [x] B17 재생 래치: /tf_static 을 자식 프레임 묶음별로 모두 보관 · 구독 때 모두 보냄. 시험: `test_replay_latched_tf_static_all`.
- [x] B18 재생이 대시보드 raw.jsonl(meta.topics 목록)과 옛 형식(dict)도 읽음. 시험: `test_replay_dashboard_raw`.
- [x] B19 실행기: exe 가 옆 src·qml 보다 오래됐으면 종료 코드 2. 시험: `test_stale_exe`. (testhooks 의 wall 은 이미 고쳐져 있음 — 재빌드로 해결)
- [x] B20 실행기: `mem = []` 를 try 앞으로. 시험: `test_server_start_failure_keeps_summary`.
- [x] B25 토픽 표(끊기면 '끊김' · '—'), 연결 화면 단계(연결 끊기면 기다림). 시험: 시나리오 `fixc_disconnect_status`.
- [x] B32 재생 throttle: 다음 송신 시각 기반 + 재생 출처는 한 주기 여유(GCRA). 시험: `test_replay_jittered_throttled_rate`.
- [x] B33 실행기: actions 의 문자열 값에도 {out}·{here}·{pi_host} 치환. 시험: `test_out_in_actions`.
- [x] B36 기록 탭 안내문 WordWrap · 본문 글꼴(경로일 때만 mono · WrapAnywhere). 캡처: `fixc_inspector_layout`.
- [x] B38 진단·노드·파라미터 빈 상태를 빈 영역 가운데(노드 탭은 '노드 정보' 패널 안), 문체 통일. 캡처: `fixc_inspector_layout`.
- [x] B39 진단 상세: key 가 빈 행을 선택으로 보지 않음, 선택 없으면 상세 모델 비움. 시험: `fixc_diag_noselect`(diagDetailCount==0).
- [x] B40 토픽 표 형식 열을 늘어나게(최대 380), 원문 패널 폭을 창 폭의 32 %(최소 440). 캡처: `fixc_inspector_layout`(topics_1920).
- [x] B49 robot.cpp: /rosout · /parameter_events 는 지연(끊김) 이벤트에서 뺀다. 시험: `tst_tools_rosout`(대조: /odom 은 지연 남음).
- [x] `checks\integration.cmd` 통과 확인(ctest 19/19, smoke, pytest 59, 시나리오 합격 26 · 건너뜀 2(파이 필요))

## 발견
- 재생 IMU 손실은 asyncio 대기 정밀도(Windows 15.6 ms) 때문이 아니라, 기록이 이미 파이 throttle 을 거친 '도착' 시각이라
  간격의 절반이 50 ms 미만인 것을 다시 50 ms throttle 로 거른 탓(이중 throttle). 실측: 고치기 전 82 %, 뒤 100 %.
- 대시보드 기록 외에 옛 record 형식은 meta.topics 가 {이름: 형식} dict 인 것도 있다(pi_idle_120s.jsonl).
- 탐색 때 exe(04:01)는 testhooks 의 wall 기록 이전 빌드였다. 지금 소스에는 trace·actions_done 에 wall 이 있다.

- 전체 검사에서 `two_clients`(17.90 Hz < 18)·`flood`(8.98 Hz < 9)의 /teleop/cmd_vel 빈도가 한 번씩 기준을 살짝 밑돌았다.
  같은 시각 다른 작업트리 대시보드 5개가 돌고 있었고, 서버 로그상 두 대시보드가 동시에 약 300 ms 멈췄다(기계 부하).
  따로 돌리면 둘 다 매번 합격(각 2~3회). 이 작업의 변경(점검 화면 QML, 재생 throttle)은 이 경로와 무관하다.

## 남은 일
- 없음(검사 통과 확인 뒤 끝).
