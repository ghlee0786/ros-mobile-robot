# PLAN — 3D 지도 축척 · 대시보드 배치 결함 수정 (t-20261020-032642-c48e)

## 목표
- 결함 1: 위에서 시점에서 지도가 LiDAR 점보다 약 0.9 배 작게 보임 → 원인 찾아 고치고 재현 시험(C++ 단위 + 시나리오 판정)을 둔다.
- 결함 2: 1280/1440/1920 에서 하단 조종 바 잘림, 레이어 패널 · 저장된 뷰 줄 · 범례 겹침 정리, 'Zero' 이름 바꾸기.

## 진행
- [x] 원인 분석: 지도 계산(중심 · 크기 · UV · flipV)은 맞았다. 위에서 시점이 4 m 위 **원근** 카메라라 라이다 높이(≈0.4 m) 점이
      바닥(지도)보다 4/(4-0.4) ≈ 1.1 배 크게 그려졌다(축척 막대는 바닥 기준). 상자 · 벽 위치를 재 보면 지도 = 축척 막대, LiDAR 만 확대.
- [x] 위에서 시점을 OrthographicCamera 로(배율 = 기존 바닥 축척과 같게), 축척 막대도 같은 `topPxPerM` 사용.
- [x] 지도 · 코스트맵 판 계산을 `msgparse::gridPlane` 하나로 모음(+ `gridCellCenter`, `planeUv`). robot.cpp renderMap · costmaplayer 가 사용.
- [x] 단위 시험 `dashboard/tests/tst_scene_mapplane.cpp`: 정사각형 · 모의 방 · 181×57 · yaw 90° · 홀수/30°.
- [x] scene.mapProbe*(U · V · Occupied · ErrPx): 렌더 경로(지도 판 노드 변환)로 알려진 벽 점의 uv · 픽셀 점유 · 라이다 높이 점과의 화면 거리.
      mapflip.json expect 에 판정 추가. 원근 카메라로 되돌리면 ErrPx 30.6 px 로 불합격하는 것 확인.
- [x] 레이어 패널 기본 접힘(접힌 폭 120), 범례 기본 접힘(Settings 기억), 저장된 뷰는 3D 머리줄의 메뉴 버튼 하나 + '기본 시점'.
- [x] 조종 바: 간격 Theme.sp4, 시험 버튼 3개 세로, 잠금 칩/버튼 세로, 속도 글자 두 줄 · 슬라이더 170, 좁으면(<1300) 조이스틱 글자 숨김.
- [x] 시나리오 scene_layout_1280 · scene_layout_1920 추가(크기만 다른 mapflip).
- [x] checks\p4_core.cmd

## 남은 일
- 없음. (normal 시나리오의 robot.distance 실패는 수정 전에도 같았다 — 조종 잠김 관련, 이 작업 범위 밖)
