# P3 Theme · 공통 컴포넌트 — 계획과 진행

## 목표
dashboard/qml 의 하드코딩 색·글자 크기를 Theme 싱글턴 토큰으로 모으고, 공통 컴포넌트를 만들고, 기존 화면을 토큰으로 다시 꾸민다. 검사: `checks\p3_theme.cmd`.

## 진행
- [x] Theme.qml (pragma Singleton, 요구서의 이름·값 그대로 + 명암비 주석, QtCore Settings category "theme")
- [x] 글꼴: Pretendard Variable 1.3.9 · JetBrains Mono 2.304 → E:\Temp\research\fonts 에서 받아 dashboard/fonts/ 로 (OFL 동봉)
- [x] 아이콘: lucide 19개 → E:\Temp\research\lucide, stroke-width 1.5 · stroke 흰색으로 바꿔 dashboard/icons/ 로 (ISC 동봉, unlock = lucide lock-open)
- [x] components/: StatusChip, Kbd, EmptyState, LoadingBar, Skeleton, IconBtn, Metric, Banner, CommandPalette, ActionBar, StatusBar, StatusExtraDash(P6 자리), StatusExtraInspector(P7 자리)
- [x] Btn / Seg / Panel / ConnectPage 다시 꾸미기
- [x] Main.qml: header(36행부터) → StatusBar + 탭, footer ActionBar, 끊김 Banner, CommandPalette, FontLoader 2개 (29~35행 그대로)
- [x] DashboardPage / InspectorPage / SceneView / RobotModel: 한 줄 안의 값만 토큰으로 (RobotModel 은 `import RobotDashboard` 한 줄 추가)
- [x] dashboard/cmake/theme.cmake (싱글턴 속성 → qt_target_qml_sources, qt_add_resources BIG_RESOURCES, Qt6::Svg)
- [x] 빌드 · smoke · 화면 캡처(다크 · 라이트 · 큰 글자), 팔레트는 임시 qml 하네스로 열고 필터 · 실행 확인
- [x] Theme.qml 밖 hex 0건

## 발견
- 이 PC 의 시스템 색 구성이 Light 라 followSystem 기본값이면 라이트로 뜬다.
- 3D 위 HUD 는 hudFill(어두움) 고정이라 라이트에서 text1 이 안 보였다 → hudText1/hudText2 토큰을 더해 SceneView HUD 줄에 썼다.
- tnum 확인: Pretendard 에서 "1111" 48px → tnum 68px("0000" 과 같음). Variable 굵기도 DirectWrite 에서 바뀐다(Normal 344 / DemiBold 359px).
- 시험 설정(HKCU\Software\RobotDashboardTest\RobotDashboard\theme)은 다른 작업과 같이 쓴다.
- 자동 키 입력(SetForegroundWindow)은 이 세션에서 막혀, 팔레트는 앱 대신 임시 qml 하네스로 확인했다.

## 남은 일
- 팔레트 · 배너를 실제 앱에서 여는 시나리오는 W1.5(P2 actions)에서 돌린다.
