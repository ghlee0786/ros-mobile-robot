# 결정 기록 — 속도 한계(최대 전진 · 후진 · 회전)

## 1. straight_drive 는 /cmd_vel 에 직접 쓴다 — 한계를 어떻게 따르게 할까
- options: (a) straight_drive 를 /teleop/cmd_vel 로 돌려 워치독을 거치게 · (b) 워치독이 저장한 한계 파일을 straight_drive 가 목표마다 읽어 스스로 자른다 · (c) straight_drive 에도 같은 파라미터를 따로 둔다
- chosen: (b) — `speed_limits.load(~/robot_link/limits.yaml)` 를 목표마다 읽어 속도(방향에 따라 전진/후진)와 회전(`w_max`)을 자른다
- why: (a) 는 워치독의 가감속 · 무명령 정지와 직진 제어가 겹쳐 동작이 바뀐다(안전 동작 불변 원칙). (c) 는 두 값이 어긋날 수 있다. 파일 하나를 두 노드가 읽으면 대시보드 · MCP 는 워치독만 바꾸면 된다
- effect: 직진 시작 뒤 바꾼 한계는 다음 목표부터 적용된다. 한계가 0 이면 `rejected (speed limit 0)`. 바꾸려면 `straight_drive_node.py` 의 `on_goal`

## 2. 한계 파일 형식
- options: (a) PyYAML 로 YAML · (b) `이름: 값` 줄만 쓰는 단순 형식(확장자 .yaml, YAML 로도 읽힘) · (c) JSON
- chosen: (b)
- why: 표준 라이브러리 unittest 로 시험하라는 요구 — PyYAML 없이 읽고 쓴다. 틀린 줄 · 범위 밖 값은 기본값으로 떨어진다
- effect: 파일을 사람이 고쳐도 된다(`max_forward: 0.30`). 경로는 워치독 · 직진 노드의 `limits_file` 파라미터로 바꿀 수 있다

## 3. min_turn_w(제자리 회전 최소 0.2 rad/s)와 최대 회전 한계가 부딪칠 때
- options: (a) min_turn_w 가 이긴다(한계보다 큰 회전이 나갈 수 있음) · (b) 한계가 이긴다(한계 < 0.2 면 느린 제자리 회전은 바퀴가 멈출 수 있음)
- chosen: (b) — `min_turn_w` 를 적용한 뒤에 자른다
- why: 사용자가 정한 한계는 넘지 않아야 한다. 최대 회전을 0.2 미만으로 두는 일은 드물다
- effect: `cmd_watchdog_node.py` 의 `step()` 에서 `limit_speed` 위치

## 4. 처음 연결 때 대시보드에 고정(자물쇠)된 값과 워치독 값이 다르면
- options: (a) 워치독 값을 가져온다(잠긴 줄도) · (b) 잠긴 줄은 대시보드 값을 워치독에 밀어 넣는다
- chosen: (a)
- why: 요구서 "처음 연결 때는 워치독 값을 읽어 슬라이더에 둔다(워치독이 기준)". 화면은 실제로 자르는 값을 보여야 한다. MCP 가 바꾼 값을 연결할 때마다 대시보드가 되돌리는 일도 없다
- effect: 자물쇠는 "슬라이더로 실수로 바꾸지 않게" 만 막는다. 바꾸려면 `SpeedLimits::refresh()` 의 `!m_synced` 분기

## 5. 슬라이더를 끄는 동안 set_parameters 를 얼마나 보낼까
- options: (a) 움직일 때마다 · (b) 놓을 때만 · (c) 마지막 변경 뒤 250 ms 에 모아서 한 번
- chosen: (c)
- why: 파이 rosbridge 는 한 코어가 상한 — 끄는 동안 서비스 호출이 쏟아지지 않게. 키보드 · 시험 훅 호출도 같은 길로 간다
- effect: `speedlimits.cpp` 의 `kPushDelayMs`

## 6. 대시보드가 쓰는 한계 = 슬라이더 값(바라는 값) vs 워치독이 돌려준 값
- options: (a) 바라는 값 · (b) 워치독 값 · (c) 둘 중 작은 값
- chosen: (a) — 키보드 · 조이스틱 배율과 '최대 속도' 위끝은 슬라이더 값을 쓴다
- why: 옛 워치독(미지원)에서도 대시보드 배율 제한이 되어야 하고, 지원하면 워치독이 어차피 자기 값으로 자른다. 값이 다르면 경고색으로 보인다
- effect: `Robot::sendTeleop` · `Robot::setMaxSpeed`

## 7. 1280 창에서 조종 바가 창보다 넓던 기존 문제(비상 정지가 화면 밖)
- options: (a) 손대지 않고 속도 한계 줄만 넣는다(셋째 줄 끝이 잘림) · (b) 위 줄(3D 뷰 + 오른쪽 열)의 최소 폭이 아래 조종 바까지 넓히지 않게 `Layout.minimumWidth: 0`
- chosen: (b)
- why: 요구서 "1280 · 1920 폭 레이아웃이 겹침 없이". 원인은 3D 뷰 도구 줄의 implicitWidth(964 px) — ColumnLayout 이 그만큼 모든 줄을 넓혀 조종 바가 1380 px 이 되었다(변경 전 스크린샷에서도 비상 정지가 보이지 않음)
- effect: 1280 에서 조종 바 · 속도 한계 · 비상 정지가 창 안에 든다(조이스틱 글자는 기존 narrow 규칙대로 숨김). 위 줄의 오른쪽 열은 여전히 1280 에서 일부 넘친다(기존 그대로, 이번 범위 밖)

## 8. 옛 워치독(파이 미배포)을 모의 서버에서 어떻게 흉내 낼까
- options: (a) `--no-speed-limits` 옵션으로 세 파라미터를 뺀다 · (b) 시나리오마다 서버 코드를 바꾼다
- chosen: (a)
- why: 실기 옛 워치독처럼 get_parameters 가 type 0 을 돌려준다. MCP 시험 · 대시보드 시나리오가 같이 쓴다
- effect: `tooling/mockbridge/mock_rosbridge.py`
