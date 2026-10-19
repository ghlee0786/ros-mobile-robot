# 결정 기록

## 1. 경보(AlertModel)를 C++ 모델로 둘지 QML 싱글턴으로 둘지
- options: src/tools/alertmodel.{h,cpp} (QAbstractListModel, QML_SINGLETON) / qml/inspector/Alerts.qml (pragma Singleton, ListModel)
- chosen: qml/inspector/Alerts.qml 싱글턴(set · remove · update(id, …), count, items)
- why: P7a 가 AlertModel 을 만들지 않았고, 상태 바 칩(StatusExtraInspector)과 점검 화면이 같은 목록을 봐야 해서 싱글턴이 필요했다. 경보 출처가 모두 QML 속성이라 QML 쪽이 코드가 가장 적다.
- effect: 경보 조건은 InspectorPage.refreshAlerts()(1 s 주기)에 모여 있다. C++ 로 옮기려면 같은 이름(set/remove/count/items)의 싱글턴으로 바꾸면 된다.

## 2. 퍼지 검색의 '필드 경로' 범위
- options: 모든 토픽을 잠깐 구독해 필드 경로를 모은다 / 원문을 연 적 있는 토픽의 경로만 쓴다
- chosen: 원문을 연 적 있는 토픽만(MessageTreeModel 에 message 속성을 더하고 PlotFeeder.flattenNumericPaths 로 편다)
- why: 검색 때문에 구독을 늘리면 파이 병목(G7)을 되살린다.
- effect: 처음 보는 토픽은 이름으로만 찾는다. TopicsTab.knownPaths 를 채우는 곳만 바꾸면 범위를 넓힐 수 있다.

## 3. rosapiSlow(경보 '지연') 판정
- options: '연결 안 됨' 이외의 모든 실패 / 시간 초과('응답 없음')만
- chosen: 시간 초과만. ServiceQueue 가 RosBridge::call 의 실패 콜백을 받게 되면서 result=false 도 실패로 들어오기 때문
- why: 없는 서비스(result=false)는 rosapi 가 느린 것이 아니다.
- effect: RosApiClient::rosapi() 의 람다 한 줄. 실패 문구 앞에는 서비스 이름을 붙인다(ServiceQueue).

## 4. 파이 없이 동작을 확인하는 방법
- options: tooling/mockbridge/scenarios/insp_*.json(요구서) / 자체 가짜 서버 스크립트
- chosen: dashboard/tests_hooks/run_inspector_test.py(가짜 rosbridge, port 0) — 메인 지시대로 시나리오 json 은 만들지 않음
- why: P1 모의 서버가 아직 진행 중이다.
- effect: 통합 단계에서 같은 판정(logCount · paramCount · set_parameters · rosapiError · alertCount · diagStaleCount)을 insp_*.json 으로 옮긴다.

## 5. 파라미터 변경 확인 방식
- options: 경고 문구만 / 바꿀 때마다 확인 팝업
- chosen: 탭 위 경고 문구 + 바꿀 때마다 확인 팝업(취소하면 편집기를 원래 값으로 되돌림). --no-publish 면 ParamClient.writable=false 로 쓰기 자체를 막음
- why: 속도 한계처럼 로봇 동작에 바로 영향을 준다.
- effect: ParamsTab.qml 의 confirm 팝업을 빼면 바로 적용된다.

## 6. P6 TimeGraph 충돌(지운 LineSeries 를 GraphsView 가 polish 때 만짐)을 어떻게 피할지
- options: destroy 를 늦춤(destroy(지연 ms)) / 선을 지우지 않고 다시 쓰기 / 점검 화면 쪽 로드 타이밍 조정
- chosen: 선을 지우지 않고 다시 씀(남는 선은 src=null 로 숨기고 점을 비움)
- why: GraphsView 가 안 보이면 polish 가 오래 안 일어나 지연 destroy 도 안전하지 않다. 다시 쓰면 지운 포인터 자체가 생기지 않는다.
- effect: qml/dash/TimeGraph.qml rebuild() 몇 줄. 선 개수는 지금까지의 최대 시리즈 수로 유지된다. Qt 가 고쳐지면 removeSeries+destroy 로 되돌릴 수 있다.
