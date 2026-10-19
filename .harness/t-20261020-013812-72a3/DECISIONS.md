# 결정 기록 (P1 모의 rosbridge)

## 1. 직진 취소 reason 철자: 'cancelled'(요구서) vs 'canceled'(실제 노드)
- options: 요구서대로 `cancelled` / 파이의 straight_drive_node.py 와 같은 `canceled`
- chosen: `canceled`
- why: 모의 서버의 목적이 파이를 그대로 흉내 내는 것이고, 대시보드는 reason 문자열을 그대로 보여 준다.
- effect: `mockbridge/sim.py` 의 `on_command` 한 곳(`self.straight_finish("canceled")`)만 바꾸면 된다.

## 2. 직진 거리 0 이하는 rejected (실제 노드는 음수 = 후진)
- options: 요구서대로 거리 ≤ 0 거부 / 실제 노드처럼 음수 후진 허용
- chosen: 요구서대로 거부(reason `distance <= 0`)
- why: 요구서에 명시. 대시보드는 양수 거리만 보낸다.
- effect: `World.straight_goal` 의 첫 조건을 `dist == 0` 으로 바꾸고 dir 부호를 넣으면 후진도 된다.

## 3. rosbridge_pingpong_test.py 가 9090 고정
- options: 스크립트 수정(금지) / 9090 이 비었을 때만 실행(아니면 skip) / 실행할 때 연결 주소만 바꿔 원 스크립트를 그대로 돌림
- chosen: 마지막 — 시험이 `python -c` 래퍼로 `websocket.create_connection` 의 ':9090' 을 시험 포트로 바꾸고 `runpy` 로 원 스크립트를 실행한다(`test_pingpong_script`). 건너뛰는 시험이 없다.
- why: 여러 작업이 같은 PC 에서 돌아 9090 을 고정으로 잡을 수 없고, 하네스 규칙상 시험을 skip 하지 않는다.
- effect: 사람이 확인할 때는 `run_scenarios.py --no-dashboard --port 9090` + `rosbridge_pingpong_test.py 127.0.0.1`.

## 4. 대시보드 trace · actions_done 형식(P2 가 아직 없음)
- options: P2 를 기다림 / 약속을 가정하고 읽기
- chosen: state.json 의 `"trace":[{"t":초, ...상태}]` 또는 `<dir>/trace.jsonl` 을 읽고, `actions_done[]` 의 각 항목에 `call`/`set` 과 `wall`(epoch 초)이 있다고 가정
- why: 시나리오 판정(was_true_between · final_eq · within_ms_after_action)에 필요하고 P2 와 맞추기 쉬운 가장 단순한 모양.
- effect: 형식이 다르면 `run_scenarios.py` 의 `check_trace` 호출부와 `_action_wall` 만 고치면 된다.

## 5. 시험 임시 폴더
- options: pytest 기본(C:\Users\...\Temp) / E:\Temp 아래
- chosen: `tests/conftest.py` 의 `pytest_configure` 에서 E:/Temp 가 있으면 `E:/Temp/pytest-mockbridge/<pid>` 를 basetemp 로
- why: C 드라이브가 거의 꽉 참(사용자 지시). 검사 명령은 고칠 수 없어 conftest 에서 정한다.
- effect: `--basetemp` 를 주면 그쪽이 우선한다.

## 6. --no-watchdog 일 때 /teleop/cmd_vel
- options: 그대로 sim 에 넣음 / 무시(워치독 노드가 없으면 중계할 노드가 없다)
- chosen: 무시
- why: 실제 파이에서 cmd_watchdog 가 없으면 /teleop/cmd_vel 은 /cmd_vel 로 가지 않는다.
- effect: `World.on_command` 의 `if not self.args.no_watchdog` 조건만 빼면 된다.

## 7. 지도 표식(mapflip 용 '+1 m 앞 점유 셀')
- options: 별도 방 파일·옵션 추가 / 기본 방에 넣기
- chosen: `rooms/default.json` 에 (0.95..1.05, -0.05..0.05) 작은 상자를 넣었다(로봇은 (0,0)에서 +x).
- why: 새 옵션 없이 mapflip 시나리오가 동작하고 /scan 에도 보여 방향 확인이 쉽다. 충돌은 시뮬레이션하지 않아 직진을 막지 않는다.
- effect: 방은 `--room` 으로 바꿀 수 있다.

## 8. pi_* 시나리오의 상태 이름 · P4 이후 시나리오
- options: 아직 없는 속성(clockSkewMs · callsPending 등)을 쓰는 시나리오를 빼기 / requires 로 표시
- chosen: `skew`(bridge.clockSkewMs), `soak`(bridge.callsPending), `halfopen`, `record_throttle` 에 `"requires":["P4-core"]` 를 붙였다. `--skip-requires` 로 건너뛴다.
- why: 요구서 7 항 · G8 속성 이름 약속. clockSkewMs 는 G8 목록에 없어 이름을 새로 정했다.
- effect: P4 가 다른 이름을 쓰면 `scenarios/skew.json` 의 path 만 고친다.

## 9. 실기 확인 결과 반영(메인 세션 추가 지시) — 요구서와 달라진 동작
- options: 요구서 원문(모르는 형식 → status error, 모르는 서비스 → "서비스 없음", cbor 는 경고 후 none) / 실기 Foxy 동작
- chosen: 실기 동작. 모르는 형식·형식 불일치 구독은 status 없이 조용히 무시(데이터도 없음, 서버 로그에만 경고), 모르는 서비스는 `result:false, values:"Service <이름> does not exist"`, `compression:"cbor"` 구독에는 아무 프레임도 보내지 않는다(`--cbor` 를 주면 cbor2 로 보냄).
- why: 추가 지시가 우선한다. 모의 서버는 실기와 같아야 대시보드 시험이 의미가 있다.
- effect: `protocol.py` 의 `subscribe` · `Conn._send`, `services.py` 의 `handle` 끝 한 줄.

## 10. CBOR 시험
- options: cbor2 가 있을 때만 `--cbor` 시험(skipif) / 기본(실기) 동작만 시험
- chosen: 기본 동작 `test_cbor_silent`(cbor 구독에는 프레임이 없고 다른 구독은 정상)만 둔다. `--cbor` 경로는 검사 의존성에 cbor2 가 없어 자동 시험하지 않는다.
- why: 시험을 건너뛰지 않는다는 규칙, 그리고 검사 명령(의존성)은 고칠 수 없다.
- effect: 검사에 `--with cbor2` 가 생기면 `--cbor` 시험을 더하면 된다.

## 11. /scan 모양과 측정 원점
- options: 레이저 장착 위치(-0.12, 0)에서 재고 frame_id=laser / 실기처럼 frame_id=base_link 로 로봇 중심에서 잼
- chosen: 기본 `--scan-frame base_link`(로봇 중심에서 잼, 장착 변환 없음). 1800 광선이면 실기와 같은 angle_min -3.124 · angle_increment 0.003483, range_min 0.15, range_max 25(`--scan-max` 기본 25). `--scan-frame laser` 를 주면 장착 위치·`--laser-yaw-deg` 에서 잰다.
- why: 실기 frame_id 가 base_link 이고 대시보드는 그때 장착 변환을 하지 않는다(항등). 그렇게 그려도 벽이 제자리에 오도록 base_link 원점에서 잰다.
- effect: `sim.py` 의 `make_scan`. IMU frame_id 는 실기 /tf_static 의 자식 이름 `imu` 로 바꿨다.

## 12. --pi-initialpose 의 효과
- options: /initialpose 를 발행만 함 / 발행하고 모의 서버 자신에게도 적용(map→odom 을 다시 맞춤)
- chosen: 발행하고 적용한다 — 5 s 마다 map 원점 `/initialpose` 를 내고, `/initialpose` 를 받은 것과 똑같이 map→odom 을 로봇이 map 원점에 오도록 다시 맞춘다. 구독자가 없어도 적용한다.
- why: 파이에서는 AMCL/SLAM 이 그 메시지를 받아 위치를 원점으로 되돌리므로, 대시보드가 보는 map→odom 도 그렇게 바뀐다.
- effect: `World.pi_initialpose`. 대시보드가 보내는 `/dashboard/initialpose` 도 같은 처리로 `--map-odom` 을 바꾼다.

## 13. noqa 제거
- options: `sys.path.insert` 뒤 import + `# noqa: E402` / 경로 조작 없이 바로 import
- chosen: 경로 조작을 없앴다. 스크립트로 실행하면 스크립트 폴더가 sys.path 첫 칸이고, 시험에서는 `tests/conftest.py` 가 폴더를 더한다.
- why: 억제 주석 없이 같은 동작.
- effect: `mock_rosbridge.py` · `pi_suite.py` 는 `python tooling/mockbridge/<스크립트>.py` 로 실행한다(`-m` 실행은 지원하지 않음).
