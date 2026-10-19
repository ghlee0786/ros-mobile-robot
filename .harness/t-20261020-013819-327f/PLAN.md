# P7a 점검 도구 C++ 모델 — 계획

## 목표
dashboard/src/tools/ 에 LogModel(+LogFilterModel), DiagnosticsModel, MessageTreeModel, ParamClient, RosApiClient 를
만들고 tests/tst_tools_*.cpp 단위 시험과 dashboard/cmake/tools.cmake 를 둔다. QML(InspectorPage · inspector/**)·
StatusBar 칩·AlertModel·시나리오(insp_*.json)는 다음 작업(P7b) 몫이다. 검사: checks\unit.cmd.

## 진행
- [x] servicequeue.{h,cpp}: 직렬 호출 + 5 s 시간 초과(공통 도우미)
- [x] logmodel.{h,cpp}: 링 버퍼 5000, active 일 때만 /rosout 구독, 일시정지, 노드 목록 / LogFilterModel(심각도 AND · 검색어 OR · hiddenNodes · 제외·강조 규칙 QSettings "inspector/logRules" · TSV 복사)
- [x] diagnosticsmodel.{h,cpp}: 'hardware_id: name' 키, 5 s STALE, 10 s streamLost, 그룹 순서, 핀(QSettings "inspector/diagPins"), 필터, setSynthetic(robot.sensors 행)
- [x] messagetreemodel.{h,cpp}: 3열 트리, topic → 'inspect' throttle 200 구독, compare/diffCount/changed, 배열 32개 + '… n개 더', 경로 복사
- [x] paramclient.{h,cpp}: list → describe → get 직렬, set_parameters, /parameter_events, writable(--no-publish 막기), editor 역할(slider/switch/text)
- [x] rosapiclient.{h,cpp}: nodes · node_details · services · service_type · topics · callService(JSON 검사, responseMs, 실패 문구), rosapiSlow
- [x] tst_tools_logmodel / diag / tree / params(가짜 rosbridge QWebSocketServer port 0) — 전부 통과
- [x] smoke(앱 8 s 기동, QML 오류 0)

## 발견
- qt_add_qml_module 의 형식 등록 코드가 헤더를 `<파일이름.h>` 로 include 하므로 tools.cmake 에서 src/tools 를 include 경로로 더했다.
- 지금 RosBridge::call() 은 result=false 응답과 연결 끊김 때 콜백을 부르지 않는다 → ServiceQueue 시간 초과(5 s)로 실패 처리.

## P4 병합 뒤 바꿀 곳
- servicequeue.cpp: call() 실패 콜백이 생기면 fail 로 바로 넘긴다(지금은 5 s 기다림).
- logmodel.cpp / diagnosticsmodel.cpp: want() 의 queueLength 인자로 /rosout 은 큰 queue, /diagnostics 는 작은 queue.
- G7 하한표: MessageTreeModel 의 'inspect' throttle 200 은 P4 의 max(하한, min(이유들)) 규칙을 따른다(지금 want 는 첫 구독의 throttle 만 쓴다).

## 남은 일(P7b)
- InspectorPage.qml 탭 UI, qml/inspector/**, StatusExtraInspector.qml(칩), AlertModel, 시나리오 insp_*.json, 요약 속성(logCount · diagStaleCount · alertCount).
