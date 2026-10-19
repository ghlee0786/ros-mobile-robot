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
- options: 스크립트 수정(금지) / 시험에서 같은 흐름 직접 구현 / 9090 이 비었을 때만 원 스크립트 실행
- chosen: 둘 다 — `test_pingpong_flow` 는 빈 포트로 같은 흐름을, `test_pingpong_script_on_9090` 은 9090 을 띄울 수 있을 때만 원 스크립트를 돌린다(쓰는 중이면 skip).
- why: 여러 작업이 같은 PC 에서 돌아 9090 을 고정으로 잡을 수 없다.
- effect: 완료 조건 확인은 `run_scenarios.py --no-dashboard --port 9090` + `rosbridge_pingpong_test.py 127.0.0.1`.

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
