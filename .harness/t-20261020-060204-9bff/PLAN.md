# INT2 계획

## 목표
1. LiDAR `/scan_compact`(UInt16MultiArray, mm) — 토픽 목록에 있으면 대시보드가 그것만 구독, 없으면 `/scan`. 모의 서버도 같은 형식으로 냄(`--no-scan-compact` 로 끔).
2. wave 2(P6 · P7b · P8b) 판정을 `tooling/mockbridge/scenarios` 시나리오로 옮김.
3. 토픽 탭 '시계 차' 요약 줄 · 실행기 종료 시 프로세스 정리.

## 진행
- [x] `msgparse::scanFromCompact`(라벨 파싱 · mm→m · 0 → null) + `parseScan`/`ScanAccumulator` 가 null 을 NaN 으로 버림
- [x] `Robot::pickScanTopic` — 토픽 목록을 받은 뒤 정함(받기 전에는 LiDAR 구독 안 함, 연결 6 s 뒤에도 목록이 없으면 `/scan`), `scanTopic` 속성, 센서 행 · 상태 바 rates · 필수 토픽 · 기록 목록이 실제 토픽을 씀
- [x] ScanAccumulator 가 `/scan_compact` 도 받음, 레이어 패널 · 연결 화면 문구
- [x] tst_msgparse: compactLabel · compactSameAsScan
- [x] 모의 서버 `/scan_compact`(sim.compact_scan), `--no-scan-compact`, pytest test_scan_compact
- [x] `--diag-stale` 를 첫 연결 기준으로(대시보드가 늦게 붙어도 진단이 한 번은 옴)
- [x] 실행기: `zero_after_action.until_action`, `goal_inverse.topic`(PoseWithCovarianceStamped), LIVE 프로세스 추적 · `kill_all`(끊겨도 정리) + pytest
- [x] 시나리오 12 개: scan_compact · scan_fallback · dash_graphs · dash_arm · insp_rosout · insp_params · insp_params_nopub · insp_service · insp_diag · scene_layers · scene_initialpose (+ normal.json 의 LiDAR 구독 규칙을 /scan_compact 로)
- [x] DashboardPage 시험 상태 속성(wheelWindowSec · wheelSpanSec · wheelLagSec · userSeriesCount · userLastPath · userLastCount)
- [x] TopicsTab '시계 차 ±n ms' (1 s 넘으면 warnText + 툴팁)
- [x] checks\integration.cmd 통과 확인

## 발견
- `QJsonValue::toDouble()` 은 null 을 0 으로 읽어 range_min 0 이면 원점 점이 생겼다 → `toDouble(qQNaN())`.
- UserGraphPanel 은 설정이 없으면 기본 계열 2 개(v · ω)를 만든다 → 시나리오는 계열 3 개 · 마지막 경로로 판정.
- `_action_wall` 은 key 가 None 이면 set 없는 항목과 맞는다(기존 동작, 건드리지 않음).

## main(FIX-A) 병합 뒤
- [x] 실패 원인: 빌드 오류 — FIX-A 와 이 브랜치가 각각 `Robot::m_connClock`(같은 뜻: 이번 연결 뒤 경과)을 선언해 `robot.h` 에 두 번 있었다. 시나리오와 안전 조건의 충돌은 아님.
- [x] FIX-A 의 선언(`m_connClock.start()`)을 남기고 이 브랜치의 선언 · `restart()` 를 지움. `pickScanTopic` 은 같은 시계를 씀.
- [x] 이 브랜치 시나리오 점검: dash_arm 은 armTeleop 뒤 drive · 잠긴 채 estop(FIX-A 도 estop 은 잠금과 무관), scene_initialpose 는 setInitialPose(FIX-A 조건 대상 아님) — actions 수정 불필요.
- [x] 빌드를 고친 뒤 `normal` 이 한 번 실패(거리 0.05 m): 병렬로 뜬 `reconnect` 창이 포커스를 가져가 DashboardPage 의 기존 안전 동작(포커스를 잃으면 drive(0,0))이 주행을 멈춤. 단독으로는 통과(간헐적). 안전 동작은 두고 normal.json 의 drive 입력을 0.5 s 마다 다시 주게 함(기준은 그대로).
- [x] checks\integration.cmd 재확인 — EXIT 0 (ctest 18/18 · smoke · pytest 54 · 시나리오 41 합격)

## 남은 일
- 없음(실기 시험은 메인 세션 담당).
