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

## 남은 일
- 없음(실기 시험은 메인 세션 담당).
