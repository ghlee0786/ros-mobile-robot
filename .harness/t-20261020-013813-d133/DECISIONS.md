## 1. --settings PATH.ini 를 실제로 어느 파일에 쓰는가
- options: (a) 조직 이름을 파일 이름으로 바꿔 DIR/NAME/RobotDashboard.ini · (b) 조직 이름은 그대로, setPath 로 DIR/NAME 폴더 아래 DIR/NAME/RobotDashboardTest/RobotDashboard.ini · (c) 디렉터리를 받는다
- chosen: (b)
- why: (a)는 조직마다 셰이더 캐시가 C: 에 새로 생겨 첫 실행이 10 s 넘게 늦고 C: 를 채운다. 인자 이름(PATH.ini)은 P1 과의 약속이라 (c)로 바꾸지 않았다.
- effect: QSettings() · QML Settings 모두 그 INI 를 쓰고 레지스트리는 건드리지 않는다. 바꾸려면 main.cpp 의 setPath 한 줄.

## 2. actions 시각 t 와 --quit-after 의 기준점
- options: 프로세스 시작 기준 · QML 로드 끝 기준
- chosen: QML 로드 끝 기준(state 의 t 도 같은 시계). 각 항목은 앞 항목(wait 포함)이 끝난 뒤 자기 t 에 돈다.
- why: 로드 시간이 부하에 따라 2~10 s 로 흔들려, 프로세스 기준이면 같은 시나리오가 actions 를 다 못 돌고 끝나는 일이 실제로 생겼다.
- effect: testhooks.cpp 의 start() 에서 m_clock.restart(). 로드 시간은 로그 "[testhooks] QML 로드 N s" 로 남는다.

## 3. 종료 코드가 겹칠 때
- options: 경고(2) 우선 · actions 실패(3) 우선
- chosen: 3 > 2 > app.exec() 결과
- why: actions 실패가 시나리오 자체의 실패라 더 구체적이다. 실패해도 끝까지 진행해 state · 스크린샷은 남긴다.
- effect: TestHooks::exitCode 에서 순서만 바꾸면 된다.
