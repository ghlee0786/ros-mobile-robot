# INT1 — 파이 없는 시나리오 불합격 7개 고치기

목표: `checks\integration.cmd`(빌드 + ctest + smoke + mockbridge pytest + run_scenarios --offline 전체) 통과.
원래 결과: E:\Temp\dashtest\20261020_024216 (합격 13 · 불합격 7). 분기점은 DECISIONS.md.

## 진행
- [x] bigmap — testhooks toJson 이 QSizeF 를 {width,height} 로 (결정 1)
- [x] estop — actions_done 에 action 원본 키 + wall(epoch) (결정 2)
- [x] skew — RosBridge.clockSkewMs (중앙값) + tst_rosbridge::clockSkewMedian (결정 3)
- [x] straight_null — 시나리오를 op "missing" 으로, 실행기 missing 버그 수정 (결정 4)
- [x] freeze/halfopen — 실행기가 --trace 를 넘기지 않았음 → 넘김. 그 뒤 드러난 것:
  - 장애 시각이 서버 시작 기준이라 늦게 붙은 대시보드와 경합 → 첫 연결 기준(결정 5, test_faults::test_fault_clock_starts_at_first_connect)
  - pong 이 rxAge 를 되살려 데이터 멈춤을 못 봄 → m_lastData(결정 6, tst_robot::linkLostWhenDataStopsButPongs)
  - trace 에 connectedChanged 줄 · wall, 종료 중 줄은 안 씀(결정 10)
- [x] soak — 누수 아님(실측 평탄), 실행기 기준에서 시작 30 s 제외 (결정 7, test_runner::test_mem_growth_skips_warmup)
- [x] 메인 세션 추가 지시: 상대 shot → 결과 폴더(결정 9), normal·estop 에 wait + optional armTeleop(결정 8)
- [x] soak 단독 재확인 (E:\Temp\dashtest\int1_soak, 증가 −28.8 MB)
- [x] testhooks 종료 가드 → freeze · wifi_bad · halfopen 재확인
- [x] 전체 실행에서 드러난 경합 2개: goal(결정 11, /map_odom), estop(결정 12, 같은 TCP 묶음)
- [x] checks\integration.cmd 전체 통과 (E:\Temp\dashtest\20261020_044049: 합격 20 · 불합격 0 · 건너뜀 2(파이))

## 발견
- 대시보드는 병렬 실행에서 서버보다 2~10 s 늦게 붙는다(QML 로드).
- 모의 서버 --freeze 중에도 websockets 는 ping 에 답한다.
- trace 마지막 줄이 앱 종료로 연결이 닫힐 때 쓰이면 final_eq 가 틀린다 → m_finished 뒤에는 쓰지 않음.
