# P1 모의 rosbridge 서버 (tooling/mockbridge/)

## 목표
파이 없이 대시보드를 자동 시험하기 위한 Python 모의 rosbridge 서버 · 기록/재생 · 시나리오 실행기 · 계약 시험.
검사: `checks\p1_mockbridge.cmd` (pytest tooling\mockbridge\tests). 같은 내용의 `tooling\mockbridge\check.cmd`.

## 진행
- [x] 기존 코드 읽기(rosbridge.cpp, robot.cpp, diffdrive_sim.py, robot_link 노드, pingpong 시험)
- [x] mockbridge/ 패키지: protocol · sim · room · faults · services · replay
- [x] mock_rosbridge.py 진입점(시작하면 `MOCKBRIDGE_PORT=<n>` 출력), rooms/default.json
- [x] record_rosbridge.py, run_scenarios.py, shot_diff.py, pi_suite.py
- [x] scenarios/*.json 22 개 (P4 이후 기대: halfopen · record_throttle · skew · soak 에 requires P4-core)
- [x] tests/ (conftest, fake_dashboard.py, data/sample.jsonl, test_protocol/sim/faults/replay/runner), check.cmd
- [x] 검사 통과(1차): 43 passed, 1 skipped
- [x] 메인 세션 추가 지시(실기 확인) 반영: --pi-initialpose, /dashboard/initialpose, scan frame base_link·실기 각도, /tf_static 3 개 따로, Foxy 무응답 동작(잘못된 형식·cbor), 없는 서비스 문구
- [x] 품질 지적 반영: noqa 제거, skip 없앰(핑퐁 원 스크립트를 포트만 바꿔 실행) → `cmd //c "checks\\p1_mockbridge.cmd"` 46 passed
- [x] 완료 조건 수동 확인: `run_scenarios.py --no-dashboard --port 9090 --only normal` + `rosbridge_pingpong_test.py 127.0.0.1 3` → 3/3, rc 0

## 발견
- Windows 의 time.monotonic 은 15.6 ms 해상도라 모든 시각을 time.perf_counter 로 쓴다(처음엔 발행률이 1/3 로 떨어졌다).
- 1800 광선 /scan: 광선마다 모든 선분을 보면 56 ms/장 → 선분이 가리는 각도 구간만 계산해 약 3~8 ms/장.
- /tf 에 odom→base_link(33 Hz)와 map→odom(5 Hz)이 섞이면 throttle 200·queue 1 에서 map→odom 이 대부분 사라진다(시험 test_tf_throttle_drops_map_odom 으로 재현).
- 2000² 지도 격자 생성 약 0.8 s (한 번 만들어 캐시).
- 하네스 가드가 내 셸 명령으로 worktree 밖(E:\Temp) 파일 쓰기를 막는다 → 수동 실험은 worktree 의 `backup/scratch/`(gitignore)에서.

## 남은 일
- 실제 대시보드와의 통합 실행(P2 시험 훅 · P4 병합 뒤): `run_scenarios.py --offline --skip-requires`.
- trace / actions_done 형식은 P2 와 맞춰 볼 것(DECISIONS 4).
