# P7b 점검 화면 UI — 계획

## 목표
P7a C++ 모델(src/tools) 위에 InspectorPage.qml 을 탭 구조(토픽·원문·로그·진단·노드·서비스·파라미터·기록·시험)로 넓히고,
상태 바 칩(Mbps·경보 개수)은 components/StatusExtraInspector.qml 에 둔다. 검사: `checks\p4_core.cmd`.

## 진행
- [x] 기존 코드 읽기(rosbridge·topicmodel·src/tools·components·testhooks)
- [x] C++ 작은 수정: ServiceQueue 가 call() 실패 콜백을 받음(서비스 이름 붙임), rosapiSlow 는 '응답 없음'만,
      /rosout queue 200 · /diagnostics queue 5, MessageTreeModel.message 속성
- [x] qml/inspector/*.qml: Alerts(싱글턴), InField, InCheck, TopicsTab, MessageTree, RecordTab, LogTab, DiagTab, NodesTab, ServicesTab, ParamsTab
- [x] InspectorPage.qml 재작성(objectName "inspector", logCount·diagStaleCount·alertCount 등 요약 속성)
- [x] StatusExtraInspector.qml 칩 2개(Mbps · 경보 개수)
- [x] tools.cmake 에 QML 등록
- [x] test.cmd · smoke · checks\p4_core.cmd 통과
- [x] dashboard/tests_hooks/run_inspector_test.py(가짜 rosbridge 로 로그 · 파라미터 set · --no-publish · rosapi 시간 초과 · STALE) 통과

## 남은 일
- 통합 단계: 아래 시나리오 json 으로 옮기기. 파이에서 Foxy rosapi(nodes · node_details · services · service_type), *_parameters 응답 형식, /rosout 부하 확인.

## 결정
DECISIONS.md 참고.

## 통합 단계에서 만들 시나리오(모의 서버 P1 완료 뒤)
insp_rosout.json, insp_params.json, insp_service.json, insp_diag.json — 요구서대로.
