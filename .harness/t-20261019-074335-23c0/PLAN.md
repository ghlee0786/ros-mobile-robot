# PLAN — Fusion 360 로봇 모델 연동 + 1차 범위 마무리

## 목표
Fusion 360 "MobileRobot" 설계에서 뽑은 메시(`dashboard/model/`)로 대시보드 3D 로봇을 그리고,
바퀴는 실측 바퀴 속도로, LiDAR 헤드는 `/scan` 이 살아 있는 동안 실제 회전수로 돌게 한다.
바퀴 간격은 펌웨어 실측값 0.34 m(CAD 0.3135 m, 약 8% 차이)를 따른다.
1차 범위(첨부 phase1_scope.md) 중 빠져 있던 3D 측정 도구 · 표시 설정 저장도 채운다.

## 진행
- [x] 첨부 3개 · 기존 대시보드(이전 하네스 작업) · prepare_dashboard_model.py 확인
- [x] 바퀴 간격 검토: 0.34 m 에 두면 바퀴 안쪽 면 0.144 m, CAD 몸체의 차축 부근 바깥 끝 0.1437 m → 틈 · 겹침 없음. 그대로 0.34 m
- [x] LiDAR 헤드: `lidar_head.obj` 를 parts.json 위치(0.0128, 0, 0.3178)에 놓고 z 축으로 회전
      (C++ `Robot::lidarHz` = 1/`scan_time`, 없으면 수신 Hz, /scan 꺼짐 · 끊김이면 0 → 멈춤). QML `FrameAnimation`
- [x] LiDAR 점을 헤드 높이에 그림 (`model.lidarPos.z`)
- [x] 거리 측정: `거리 측정` 버튼 → 바닥 두 번 클릭 → 거리 · Δx · Δy (odom 프레임)
- [x] 설정 저장: 카메라 · 레이어(QtCore `Settings`, SceneView) · 창 크기(Main)
- [x] README 갱신
- [x] 빌드(`dashboard\build.cmd`) 통과, 실제 Pi(192.168.1.201)에 붙여 스크린샷 확인
      (scratch/before.png · measure.png · close2.png · dbg.png — dbg 는 임시 표시로 lidarHz 12.93 · 각도 변화 확인 후 제거)
- [ ] (사용자) 로봇을 움직여 바퀴 회전 방향 확인 — 무인이라 주행 명령을 보내지 않았다

## 발견
- 몸체 OBJ(91 MB) 로드 포함 창이 뜨기까지 약 5 s
- /scan 의 `scan_time` 으로 본 RPLIDAR A3 회전수 ≈ 12.9 Hz
- 바퀴 회전: /wheel_states 는 유니사이클 규약(둘 다 + 면 전진) → base_link y 축 + 회전이 전진 굴림

## 검증 도구
- `scratch/shot.ps1 -Out <png> [-WaitSec n] [-Clicks "x,y,s;u:x,y,s;w:x,y,s"]` — 화면 밖에 띄워 창만 캡처.
  클릭 좌표는 클라이언트 좌표(캡처 이미지 x-8, y-31). `u:` 휠 확대, `w:` 휠 축소.
  주의: 카메라 · 레이어를 바꾸면 사용자 설정(레지스트리)에 남으므로 끝에 원래대로(추적 · 레이어 기본) 돌려 둔다.
