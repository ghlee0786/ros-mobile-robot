# P8b 3D 장면 QML — 계획

## 목표
SceneView.qml · qml/scene/** 에 초기 위치 도구, LiDAR 잔상·색, 지도 팔레트·코스트맵·footprint, TF 축·페이드,
3인칭 추적·저장된 뷰·Focus, /amcl_pose 공분산 타원, 레이어 목록 패널을 더한다. src/scene 의 'P4 병합 뒤 바꿀 곳' 반영.
완료 판정: `cmd //c "checks\\p4_core.cmd"` (test.cmd + smoke 12 s) 종료 코드 0.

## 진행
- [x] 기존 코드 읽기, 기준 검사 통과 확인(16 시험, smoke 정상)
- [x] C++: SceneSource /map_odom + msgparse::mapToOdom, CostmapLayer::toImage → msgparse occupancyToImage(costmap),
      ScanAccumulator 장착 변환(/tf_static), ColorPointInstancing points 모드(경로 그라디언트), TfTree 정적 프레임 유지
- [x] 시험 추가: tst_scene_scanacc(장착 변환 · 그라디언트), tst_scene_costmap(/map_odom), tst_scene_tftree(꺼져 있을 때 정적)
- [x] QML: qml/scene/SceneDots · ScenePoseArrow · SceneCovEllipse · SceneTfAxes · SceneLayerPanel, SceneView 재구성
- [x] 뷰 큐브 윗면 글자 방향, 지도 체크박스 ↔ robot.mapWanted
- [x] scene.cmake 에 QML 등록
- [x] 지도 상하 뒤집힘 발견 → GridPlane Texture.flipV 로 수정, dashboard/tests_hooks/run_scene_test.py 로 확인
- [x] 검사 통과, run_hooks_test.py · run_scene_test.py 통과

## 발견
- scene.setView("없는 시점") 은 TypeError 를 내야 한다(tests_hooks 가 기대) — 그대로 둠.
- MapTexture(textureData)는 이미지 첫 행을 v=0 에 놓아 지도가 상하로 뒤집혀 그려졌다(원점 yaw 0° 지도 + y=0 줄 셀로는 안 드러남).
- 같은 토픽을 이미 다른 용도가 구독 중이면 rosbridge 에 다시 subscribe 하지 않아 /tf_static 이 다시 오지 않는다.
- fps(이 PC, 모델 포함, 추적 시점): 레이어 끔 18.3~20.2 → 전부 켬(잔상 10 s · 20000 점 포함) 16.1~17.2. 모델 없이는 56~57 → 55.
- 모의 서버(P1) 미완 → 시나리오 json 은 만들지 않음. 통합 단계에서 만들 시나리오: scene_initialpose · scene_layers · scene_mapflip · scene_follow.

## 남은 일
- 통합 단계: tooling/mockbridge/scenarios/scene_*.json (run_scene_test.py 의 판정을 옮기면 된다).
- 실기 확인: /initialpose 가 PC AMCL 까지 닿는지, 코스트맵 · footprint · /amcl_pose 도달, 실제 LiDAR 장착 각도.
