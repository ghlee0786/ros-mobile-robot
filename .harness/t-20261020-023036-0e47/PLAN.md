# PLAN — README 그림 자료 (docs/assets/)

## 목표
README 에 넣을 애니메이션 SVG 7개 + 검사 스크립트 check_assets.py 를 docs/assets/ 에 만든다.
README.md·다른 디렉터리는 고치지 않는다. 완료 조건: `checks\svg_assets.cmd` 통과.

## 진행
- [x] check_assets.py (XML·script/foreignObject·외부 href·viewBox·300 KB, 위반 시 종료 1 — 일부러 틀린 파일로도 확인)
- [x] hero.svg — 제목·부제·칩, 회전 LiDAR 빔 + 벽 점(SMIL, 빔과 같은 시계), 경로 그리기
- [x] architecture.svg — PC/WSL/파이/하드웨어, 방향별 색 패킷(animateMotion), 토픽 라벨
- [x] dataflow.svg — 6 s 주기: 정상 → 끊김 → 타이머 → STOP·0 패킷·바퀴 멈춤, 아래 타임라인
- [x] metrics.svg — 8 카드, 막대 차오름, `id="tests-count"`(16)
- [x] features.svg — 허브 + 네 갈래, 차례로 나타남
- [x] inspiration.svg — 카드 6개, "가져온 것" 한 줄
- [x] roadmap.svg — 완료 실선(그려짐) / 지금 / 다음 점선 + 깜박이는 점
- [x] headless Chrome 으로 여러 시각 렌더링 확인(겹침·잘림 수정)
- [x] checks\svg_assets.cmd 통과

## 발견
- headless Chrome `--virtual-time-budget` 에서 CSS 애니메이션은 SMIL 과 시계가 어긋난다 → 동기가 필요한 것은 모두 SMIL 로.
- animateMotion 에 양수 begin 을 주면 시작 전 패킷이 (0,0) 에 보인다 → 음수 begin 사용.
- 렌더링 도구: `.harness/t-20261020-023036-0e47/render.sh <name> <w> <h> [ms]` (Chrome 프로필·출력은 이 폴더 아래, 확인 후 지움).

## 남은 일
- 없음. README 본문은 메인 세션이 쓴다(시험 개수는 metrics.svg 의 `id="tests-count"`).
