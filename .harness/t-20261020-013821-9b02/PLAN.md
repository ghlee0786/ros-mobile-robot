# P8a — 3D 장면 C++ 모델 (wave 1)

## 목표
dashboard/src/scene/ 에 QML 없이 시험할 수 있는 장면 모델을 만들고 tests/tst_scene_*.cpp 로 검증한다.
SceneView.qml · RobotModel.qml · qml/scene/** · 시나리오는 다음 작업(P8b)이 한다.

## 진행
- [x] scenemath: yawOf, rainbow(RViz getRainbowColor), covEllipse(2σ 닫힌 해), appendDense
- [x] SceneSource(QML_ANONYMOUS 바탕): bridge · active(기본 꺼짐, 켜면 want) · received · /tf 의 map→odom 기억 · toOdom
- [x] ScanAccumulator: 받은 시점 자세로 odom 고정, decaySec 링, 마지막 묶음 유지, 20000 점 상한, flat/range/intensity 색, 묶음 알파
- [x] ColorPointInstancing: source(ScanAccumulator) 점별 색 인스턴스
- [x] TfTree: /tf(200 ms) · /tf_static(0 ms) 'tfview', 고정 프레임 기준 자세, frames(QVariantList), fade(1/3 정상·1/3 회색·1/3 알파)
- [x] AmclPose: topic(/amcl_pose 또는 /odom), map→odom, 2σ 타원, yaw 2σ(yawSpread), valid(공분산 0 아님)
- [x] CostmapLayer(topic · throttleMs, costmap 팔레트 0 투명, z=0.005) · FootprintLayer(닫힌 선)
- [x] cmake/scene.cmake, 시험 4개(scanacc · tftree · cov · costmap), dashboard\test.cmd · smoke 통과

## 다음 작업(P8b)이 쓸 것
- QML 이름: ScanAccumulator{bridge, active, robotX/Y/Theta, decaySec, colorMode, color, pointCount},
  ColorPointInstancing{source, size}, TfTree{active, fixedFrame, timeoutSec, frames, frameCount},
  AmclPose{topic, active, valid, x, y, yaw, axisMajor, axisMinor, angle, yawSpread},
  CostmapLayer{topic, throttleMs, active, received, image, center, yaw(deg), size}, FootprintLayer{active, received, points}
- SceneView 요약 속성: costmapReceived ← CostmapLayer.received, tfFrameCount ← TfTree.frameCount,
  scanAccumPoints ← ScanAccumulator.pointCount
- 코스트맵 이미지는 Robot::mapImage 와 같은 행 방향(0 행 = 지도 맨 위). SceneView 의 지도 텍스처 설정을 그대로 쓰면 된다.

## 추가 지시(파이 실기) 반영
- [x] DECISIONS 6: /dashboard/initialpose 는 P4 몫, scan frame_id=base_link 는 지금 항등 처리와 맞음, /tf_static throttle 0 유지 · 정적 변환 3개 시험 추가

## 남은 일
- 저장된 뷰(카메라 북마크)는 QML Settings 로 P8b 에서(DECISIONS 3).
