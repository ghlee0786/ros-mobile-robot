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

## 추가 지시(main 의 P6 병합 뒤 smoke 충돌) — 완료
- 증상: 앱이 0xC000041D 로 몇 초 뒤 죽음. gdb attach 스택: QGraphsView::updatePolish → QMetaObject::cast(지워진 시리즈).
- 원인: P6 qml/dash/TimeGraph.qml rebuild() 가 view.removeSeries(l) 직후 l.destroy(). Qt 6.8 QGraphsView 는 지운 시리즈를
  다음 polish 까지 m_cleanupSeriesList 에 날 포인터로 들고 있다. 시작 때 rebuild 가 두 번(onSeriesChanged + onCompleted) 돌아
  polish 전에 destroy 되면 죽는다. 점검 화면이 로드 타이밍을 바꿔 드러났을 뿐 점검 화면 코드는 원인이 아님(이분 탐색으로 확인).
- 수정: TimeGraph.rebuild() 가 LineSeries 를 지우지 않고 다시 쓰고, 남는 선은 src=null 로 숨김(P6 파일 최소 수정).
- 확인: smoke 4회 연속 통과, checks\p4_core.cmd 통과(ctest 17/17), run_inspector_test.py 통과.
