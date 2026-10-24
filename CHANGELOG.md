# 변경 내역 · Changelog

이 파일의 형식은 [Keep a Changelog](https://keepachangelog.com/ko/1.1.0/) 를, 버전은 [Semantic Versioning](https://semver.org/lang/ko/) 을 따릅니다.
릴리스는 `tooling/release/release.py` 가 이 파일의 해당 항목을 GitHub 릴리스 본문으로 올리고, 대시보드는 그 본문을 '변경 내역' 으로 보여 줍니다.

## [0.1.0] - 2026-10-20

### 추가
- **설치 파일** — `ROSMonitor-Setup-0.1.0.exe`(Inno Setup): 제품 특징 소개 · 설치 위치(사용자별/모든 사용자) · 기본 설정(로봇 주소 · rosbridge 포트 · 업데이트 확인 · 바탕 화면 바로 가기). 업그레이드하면 이전 프로그램 파일을 지우고 새로 깔며 설정은 그대로 둡니다.
- **업데이트 알림** — 시작 10 초 뒤와 6 시간마다 GitHub Releases 를 확인해 새 버전 배너(변경 내역 · 다운로드하고 설치 · 이 버전 건너뛰기 · 나중에)를 띄웁니다. 주행 중에는 설치를 막습니다. `Ctrl+K` → '업데이트 확인'.
- **3D 뷰** — Fusion 360 실제 로봇 모델(경량 glb, 바퀴 · LiDAR 헤드가 실제 데이터로 회전) · 추적 / 3인칭 / 위에서(직교) / 자유 시점 · 뷰 큐브 · 거리 측정 · 저장된 뷰.
- **레이어** — LiDAR 점(잔상 · 거리/강도 색) · 이동 경로 · 지도(map/costmap/raw 팔레트) · 전역 · 지역 코스트맵 · footprint · TF 축 · AMCL · 오도메트리 공분산 타원.
- **내비게이션** — 누른 곳 = 목표, 끄는 쪽 = 도착 방향(RViz 2D Goal Pose 방식) → `/goal_pose` · 초기 위치 도구 · 계획 경로 · 취소.
- **그래프** — PlotJuggler 식 메시지 경로 그래프 · 시간 창 · 일시정지 · 확대 · X 묶음 · 변환(미분 · 이동평균 · 적분 …) · 구간 통계 · CSV/PNG.
- **데이터 점검** — 토픽 표(Hz · 지연 · KB/s · 기록) · 원문 메시지 트리 · `/rosout` 로그 · 진단 · 노드 · 서비스 호출 · 파라미터 편집.
- **조종 · 안전** — 키보드 · 화면 버튼 · 가상 조이스틱 · 조종 잠금(길게 눌러 해제 · 30 초 무입력 잠김) · 비상 정지 · 1 m 보정 직진 시험 · 끊김 · 반쯤 열린 연결 감지.
- **디자인** — 다크/라이트 테마 · 디자인 토큰 · Pretendard · lucide 아이콘 · `Ctrl+K` 명령 팔레트 · 상태 바.
- **시험 훅** — `--url --actions --state --screenshot --trace --quit-after --no-publish --no-model` 와 모의 rosbridge 서버로 로봇 없이 검증.

### 바뀜
- 설치본(실행 파일 옆에 `model/`)에서는 기록(`records/`)을 설치 폴더 대신 `문서\RobotDashboard\records` 에 씁니다.
