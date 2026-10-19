# 결정 기록 (P8b 3D 장면 QML)

## 1. 뷰 큐브 윗면 글자 방향과 위에서 시점의 큐브 회전
- options: (a) 윗면 글자 위쪽을 +x(앞)로 돌리고 큐브는 본 화면과 똑같이 돈다 — 위에서 시점에서는 글자가 90° 누워 보임 / (b) 글자 위쪽 +x 에 더해, 위에서 시점의 큐브만 +x 가 위로 오게 yaw -90 으로 보인다 / (c) 위에서 시점 본 화면도 +x 를 위로 돌린다
- chosen: (b)
- why: 메인 세션 지시("위에서 시점의 큐브는 로봇 앞(+x)이 화면 위쪽이 되게 읽혀야 한다")를 글자 그대로 따르면서, 요구서 (8)의 "위에서 시점 +x = 화면 오른쪽"(지도 방향 판정 · 축척 막대)은 바꾸지 않는다.
- effect: 위에서 시점에서 큐브는 본 화면보다 90° 돌아 보인다. 본 화면과 맞추려면 SceneView.qml 뷰 큐브 Node 의 `root.cam === "위에서" ? -90 : root.viewYaw` 를 `root.viewYaw` 로 되돌린다.

## 2. 지도 · 코스트맵 텍스처 상하 뒤집힘 수정 위치
- options: MapTexture(C++)에서 행 순서를 뒤집기 / SceneView 의 Texture.flipV
- chosen: SceneView 의 GridPlane 에 `flipV: true`
- why: 요구서가 "MapTexture 쪽이 아니라 SceneView 의 텍스처 flip 이나 UV 로 고친다"고 정했다. 원점 yaw 90° 지도에서 점유 셀이 로봇 뒤에 나오는 것을 가짜 서버 캡처로 확인했다(yaw 0° 에서는 셀이 y=0 줄이라 드러나지 않았음).
- effect: 지도 · 전역/지역 코스트맵 모두 같은 GridPlane 을 쓴다. 되돌리려면 flipV 한 줄을 지운다.

## 3. LiDAR 잔상의 레이저 장착 변환
- options: (a) ScanAccumulator 가 지나가는 /tf_static 의 base_link→레이저 변환을 적용 / (b) 장착을 무시(실기 /scan frame_id 는 base_link) / (c) 손으로 정하는 장착 yaw 설정
- chosen: (a). 잔상이 0 s 여도 LiDAR 레이어가 켜져 있으면 ScanAccumulator 를 돌려(그리기는 robot.scanPoints) 연결 때 한 번 오는 /tf_static 을 놓치지 않게 했다.
- why: Robot::onScan 과 같은 규칙이고 RosBridge 공개 API 만으로 된다. 손 설정(c)은 P4 (16) 쪽 범위라 더하지 않았다.
- effect: LiDAR 레이어가 꺼진 채 연결하면 장착 변환을 못 받을 수 있다(실기는 base_link 라 영향 없음). 손 설정이 필요하면 ScanAccumulator 에 mount 속성을 더한다.

## 4. TfTree 의 /tf_static 을 꺼져 있을 때도 받아 두기
- options: (a) 켜져 있을 때만 받기(원래) / (b) 꺼져 있어도 bridge 로 지나가는 /tf_static 을 받아 두고 끌 때 정적 프레임은 남김
- chosen: (b)
- why: 같은 토픽을 Robot 이 이미 구독하고 있으면 TfTree 가 켜질 때 rosbridge 가 다시 subscribe 하지 않아 정적 TF 가 다시 오지 않는다(가짜 서버 시험에서 laser · imu_link 프레임이 빠짐).
- effect: 정적 프레임은 앱이 도는 동안 남는다. 다른 로봇에 붙을 때 지우려면 TfTree 에 clear 를 더해 Robot.resetSession 과 묶는다.

## 5. 저장된 뷰의 저장 형식
- options: Settings 의 var(QVariantList) / JSON 문자열 배열
- chosen: `bookmarks` 문자열 속성에 JSON 배열 [{name, mode, pivot:[x,y,z], dist, yaw, pitch}]
- why: INI · 레지스트리 어디에 저장돼도 그대로 되살아나고, state 덤프(단순 속성만)에도 보인다.
- effect: 이름은 "뷰 n" 으로 자동으로 붙는다(이름 입력 칸은 두지 않음). 피벗은 odom 절대 좌표로 저장하고 되살릴 때 로봇 기준 오프셋으로 바꾼다.

## 6. 레이어 기본값과 상태 문구
- options: 새 레이어(코스트맵 · footprint · TF · AMCL · /odom 공분산)를 기본 켬 / 기본 끔
- chosen: 기본 모두 끔(요구서), 켤 때만 구독. 데이터가 없으면 행에 '수신 없음 · PC Nav2(도메인 8) 연결 필요' 등.
- why: 파이 rosbridge 부담(G7)과 요구서.
- effect: SceneView 의 show* 속성 기본값으로 바꾼다.
