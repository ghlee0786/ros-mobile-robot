# 결정 기록 — Nav2 결과 · 실패 이유를 대시보드 · MCP 에 알리기

## 1. 구독 직후 이전 목표의 상태(5 s 마다 다시 오는 끝 상태)를 어떻게 가릴까
- options: (a) stamp 로 가림(PC · Windows 시계 차이에 약함) · (b) 보낸 목표 좌표 + seq 로 가림 · (c) 가리지 않음
- chosen: (b) `msgparse::navStatusMatches` — 목표가 보낸 것과 2 cm 안이고, seq 가 "이전에 받은 seq + 1" 이상이거나 상태가 진행 중(planning · running)이면 받는다
- why: 시계 차이(clockSkew)를 타지 않고, MCP 등 다른 곳에서 보낸 목표의 상태를 대시보드 화면에 섞지 않는다. 진행 중 상태는 늘 마지막 목표의 것이라 PC 노드가 다시 떠 seq 가 1 부터 시작해도 이어 받는다.
- effect: 다른 곳(MCP)에서 보낸 목표는 대시보드 3D 뷰에 보이지 않는다(이벤트 로그에도). 보이게 하려면 navStatusMatches 의 좌표 조건을 빼면 된다.

## 2. 자율주행에서 벗어나 구독을 끊을 때 진행 중이던 목표 표시
- options: (a) 마지막 상태(이동 중)를 그대로 둠 · (b) 대시보드가 스스로 '취소됨' 으로 바꿈 · (c) 취소 결과가 올 때까지 잠시 더 구독
- chosen: (b) `Robot::stopNavStatus` — sent · planning · running 이면 canceled 로 바꾸고 "Nav2 목표 취소됨" 이벤트
- why: 모드를 바꾸면 cancelGoal 이 늘 같이 나가고(기존 동작), 구독은 자율주행 중에만 하라는 요구(rosbridge 한 코어 상한)를 지키면서 화면이 "이동 중" 에 멈추지 않게.
- effect: 실제 취소가 PC 에 닿지 않았어도 화면은 '취소됨' 이다(그 경우는 기존 noteDropped 경고가 따로 뜬다). (c) 로 바꾸려면 stopNavStatus 에서 구독 해제를 몇 초 늦추면 된다.

## 3. 보낸 뒤 /nav/status 가 오지 않을 때
- options: (a) '보냄' 으로 계속 둠 · (b) 5 s 뒤 안내 문구로 바꿈
- chosen: (b) 상태 "sent" 가 5 s 넘으면 "Nav2 상태(/nav/status)가 오지 않습니다 (PC 의 nav_goal_bridge · run_bridge.sh 확인)" 와 이벤트 한 줄
- why: 이번 변경 뒤에는 nav_goal_bridge 가 없으면 /dashboard/goal_pose 를 받는 노드가 없어 목표가 아예 Nav2 에 가지 않는다 — 사용자가 바로 원인을 알게.
- effect: kNavSilentMs(robot.cpp) 로 시간 조정.

## 4. 실패 배너 모양(한 줄이 너무 김)
- options: (a) 이유 전체를 Banner 한 줄 · (b) "원인 — 할 일" 을 나눠 Banner 에는 원인, 아래 HUD 줄에 할 일
- chosen: (b) — 한 줄이면 1440 폭에서도 레이어 패널 · 뷰 큐브와 겹쳤다(시나리오 nav_fail 캡처로 확인)
- why: 공통 컴포넌트(Banner · Btn)와 토큰만 쓰면서 겹치지 않게. nav_goal_bridge 의 reason 은 모두 "원인 — 할 일" 꼴로 썼다.
- effect: reason 에 " — " 가 없으면 아래 줄이 사라지고 배너만 남는다.

## 5. 저장소 밖(쓰기 권한 없음)에서 함께 바뀌어야 하는 것
- options: — (기록만)
- chosen: 아래를 대화 세션 · 다른 작업에 넘긴다
- why: 쓰기 범위 밖이거나 실기 배포가 필요하다.
- effect:
  - **파이 배포 필요**: `tooling/rpi/robot_link/nav_relay_topics.sh` 의 순서가 바뀌었다(/goal_pose 가 _to_pc 에서 빠지고 맨 뒤로, /nav/status 가 NAV_RELAY_TO_ROBOT 맨 뒤에 추가). 파이의 robot-nav-relay.service 가 쓰는 같은 파일(~/robot_link/nav_relay_topics.sh)을 PC 와 **동시에** 바꾸고(`tooling/rpi/deploy_robot_link.sh` 가 이 파일을 올린다) 서비스를 다시 띄워야 한다 — 한쪽만 바뀌면 인덱스가 어긋나 토픽이 엉뚱한 이름으로 나간다.
  - PC 의 `run_nav.sh` 를 다시 띄우면 nav_goal_bridge 가 함께 뜬다(run_bridge.sh). bt_navigator 는 여전히 PC 의 /goal_pose 를 구독하지만 이제 그 이름으로는 아무것도 오지 않는다(RViz 2D Goal Pose 는 그대로 쓸 수 있다 — 단 그 결과는 /nav/status 에 나오지 않는다).
  - 루트 `CHANGELOG.md` 는 쓰기 범위 밖이라 고치지 않았다 — 다음 릴리스 때 "Nav2 결과 · 실패 이유 표시(/nav/status)" 항목을 더하면 된다.
