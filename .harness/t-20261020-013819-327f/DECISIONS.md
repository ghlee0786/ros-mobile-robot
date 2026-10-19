## 1. call() 실패 콜백이 없는 지금 API 에서 실패를 어떻게 알까
- options: RosBridge 수정 / 클라이언트 쪽 시간 초과 / 실패 무시
- chosen: tools/servicequeue 가 직렬 대기열 + 5 s 시간 초과로 실패 처리
- why: rosbridge.* 는 건드리면 안 되고, 요구서의 '5 s 뒤 실패 문구·경보' 와 맞는다
- effect: result=false 도 5 s 뒤에야 실패로 보인다. P4 병합 뒤 servicequeue.cpp 에서 실패 콜백을 fail 로 연결하면 바로 보인다

## 2. 로그 일시정지 중 들어오는 줄
- options: 버린다 / 따로 모았다가 풀 때 넣는다
- chosen: 버린다(LogModel::append 가 paused 면 무시)
- why: 가장 단순하고 링 버퍼 상한과 충돌이 없다
- effect: 일시정지 동안의 로그는 남지 않는다. 바꾸려면 append 에서 보류 목록에 쌓고 setPaused(false) 때 넣는다

## 3. 메시지 경로의 배열 표기
- options: `/scan.ranges[3]` / `/scan.ranges.3`
- chosen: `[i]` 표기(`/odom.pose.pose.position.x`, `/scan.ranges[3]`)
- why: 요구서 예시와 같은 점 표기에, 배열은 흔한 대괄호 표기
- effect: P6 사용자 그래프가 다른 표기를 쓰면 messagetreemodel.cpp 의 build()·flatten() 두 곳만 바꾼다

## 4. 진단 합성 입력 형식
- options: Robot 을 직접 읽기 / QML 이 robot.sensors 행을 넘기기
- chosen: Q_INVOKABLE setSynthetic(rows) — label·state·detail 행(지금 robot.sensors 형식, 영어 ok/warn/error/stale 도 받음)
- why: robot.* 의존 없이 P4 sensorModel 형식이 바뀌어도 QML 에서 맞출 수 있다
- effect: 상태 문구 매핑은 DiagnosticsModel::levelFromState 한 곳

## 5. 노드 체크 목록의 표현
- options: 보일 노드 목록 / 숨길 노드 목록
- chosen: hiddenNodes(체크 해제한 노드)
- why: 새 노드가 나타나면 기본으로 보인다
- effect: QML 체크박스는 !hiddenNodes.includes(name) 으로 묶는다
