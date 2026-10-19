## 1. LiDAR 데이터 색: 요구서의 #4CC3E8 대신 기존 분홍 #E0457B
- options: 토큰 표 그대로 lidar #4CC3E8 / 비판 수정안 7대로 분홍 유지
- chosen: `Theme.lidar` = "#E0457B" (이름은 약속대로 lidar)
- why: 추가 지시에서 비판 수정안을 반영하라고 했고, #4CC3E8 은 trail #56CCB2 와 비슷한 청록이라 3D 에서 구분이 어렵다
- effect: LiDAR 점 · 범례 · 2D 미니맵 점이 분홍. Theme.qml 한 줄만 바꾸면 된다

## 2. 3D 재질 · 조명 · 뷰 큐브 색: 예외 대신 Theme.mat* 데이터 토큰
- options: hex 를 그대로 두고 예외로 보고 / Theme 에 고정 데이터 토큰을 정의
- chosen: matAluminum · matDarkPlate · matRubber · matHub · matLidarBody · matLidarTop · matGround · sceneAmbient · viewCubeFace · viewCubeEdge (테마와 무관한 고정값)
- why: "Theme.qml 밖 hex 0건" 완료 조건을 지키면서 값은 바뀌지 않는다
- effect: 3D 모습은 그대로. 값은 Theme.qml 에서만 고친다

## 3. 라이트 테마의 3D HUD 글자색
- options: text1/text2 그대로(라이트에서 어두운 HUD 위 어두운 글자) / 고정 밝은 토큰 추가
- chosen: hudText1 #E8EAED · hudText2 #A3A9B2 를 더해 SceneView 의 HUD 줄에만 썼다
- why: canvas3d · hudFill 은 라이트에서도 어둡다는 요구서 규칙과 맞추려면 그 위 글자도 고정이어야 한다(라이트 캡처에서 읽히지 않음을 확인)
- effect: 다크에서는 값이 text1/text2 와 같다. 다른 묶음이 HUD 를 만들 때도 이 토큰을 쓰면 된다

## 4. 테마 전환을 어떻게 저장하나
- options: dark 를 직접 저장 / followSystem + 따로 저장한 선택값
- chosen: Settings("theme")에 followSystem(기본 true)과 darkChoice 를 두고, `dark = followSystem ? 시스템이 Light 아님 : darkChoice`. 팔레트의 "테마 전환"은 followSystem 을 끄고 반대 값을 저장한다
- why: 요구서의 "기본 true, 시스템이 Light 이면 false" 와 followSystem 설정 이름을 함께 만족한다
- effect: 이 PC 처럼 시스템이 라이트면 처음에는 라이트로 뜬다. 다시 시스템을 따르게 하는 명령은 없다(설정 followSystem=true 로 돌리면 된다)

## 5. 크기 상수의 가장 가까운 토큰
- options: 수치상 가장 가까운 값 / 역할이 같은 토큰
- chosen: pixelSize 10·11→fsCaption, 12→fsSmall, 13→fsBody, 14→fsLabel, 15~18→fsTitle, 22→fsHeading, 34(뷰 큐브 글자)→fsMetric. radius 999→rPill, 14·12·10→rLg, 8→rMd, 4→rXs
- why: 3D 뷰 틀(14)은 Panel 과 같은 카드라 rLg 로 맞췄다
- effect: 글자가 1~2px, 둥근 모서리가 2~4px 바뀐다. 각 줄의 토큰 이름만 바꾸면 된다

## 6. 그래프 선 색 배정
- options: 기존 색과 가장 가까운 series 값 / 의미별 배정
- chosen: 바퀴 목표 L series[1](주황) · 목표 R series[4](노랑) · 실측 L series[0](파랑) · 실측 R series[3](보라), gyro series[2] · accel series[0], 점검 그래프 목표 series[1] · 실측 series[0]; 3D 경로 trail, 계획·목표선 plan, 측정·드래그 goalDrag, Nav2 목표 ok, 축 axisX/Y/Z
- why: 기존 색 계열을 최대한 유지
- effect: 범례 문자열도 같은 토큰을 쓰므로 함께 바뀐다
