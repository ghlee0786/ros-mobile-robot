## 1. 지도 축소 결함을 어떻게 고칠까
- options: (a) 위에서 시점을 직교 카메라로 · (b) LiDAR 점을 바닥 높이로 내려 그리기 · (c) 지도 판 크기 보정
- chosen: (a) OrthographicCamera (RViz TopDownOrtho 와 같은 방식)
- why: 지도 계산은 맞았고 원인은 원근 시차. 직교면 높이와 상관없이 축척 막대 · 지도 · LiDAR 가 모두 일치한다. (c) 는 틀린 값을 덮는 것.
- effect: 위에서 시점은 원근감이 없다. 되돌리려면 SceneView.qml 의 topCam 을 PerspectiveCamera 로(그러면 mapflip 판정이 실패한다).

## 2. 저장된 뷰 줄을 머리줄에 어떻게 넣을까
- options: 콤보 + 저장 + 삭제 버튼 그대로 · 메뉴 버튼 하나(불러오기 · 지금 뷰 저장 · 삭제 하위 메뉴)
- chosen: 메뉴 버튼 '저장된 뷰 (n) ▾' + '기본 시점'
- why: 그대로 넣으면 1280 에서 머리줄이 길어 오른쪽 센서 패널이 화면 밖으로 밀렸다.
- effect: 북마크 조작이 한 번 더 누르는 메뉴로 바뀐다. SceneView.qml 머리줄의 bmBtn 에서 바꾼다.

## 3. 'Zero' 버튼 이름
- options: '원점 맞춤' · '기본 시점'
- chosen: '기본 시점'
- why: 실제 동작이 setView("홈") — 기본 각도 · 로봇 중심 · 거리 2.5 m 로 돌아가는 것이라 '원점'보다 뜻이 정확하다.
- effect: 글자만 다르다(SceneView.qml).

## 4. 조종 바를 1280 에 맞추는 방법
- options: 두 줄로 줄바꿈 · 요소를 세로로 쌓고 간격 줄이기 · 가로 스크롤
- chosen: 시험 버튼 3개 · 잠금 칩/버튼을 세로로 쌓고, 간격 sp4, 좁은 창(<1300)에서는 조이스틱 옆 글자 숨김
- why: 바 높이(3D 영역)를 늘리지 않고 비상 정지를 늘 오른쪽 끝에 둔다.
- effect: DashboardPage.qml foot · ArmControl.qml(Row→Column) · Joystick.qml showLabel.
