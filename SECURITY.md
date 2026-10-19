# 보안 · Security

- 로봇의 rosbridge(`:9090`)와 내비 중계(`:7701/7702`)에는 **인증이 없습니다.** 신뢰하는 LAN 에서만 쓰고, 공유기 밖으로
  포트를 열지 마세요. 원격 속도 명령은 모두 파이의 `cmd_watchdog`(0.5 s 무입력 정지)을 거칩니다.
- The robot's rosbridge (`:9090`) and the nav relay (`:7701/7702`) are **unauthenticated**. Use them on a trusted LAN only and never
  forward them to the internet. All remote velocity commands pass the Pi's `cmd_watchdog` (stops after 0.5 s without input).
- 취약점은 공개 이슈 대신 GitHub 의 **Security → Report a vulnerability** 로 알려 주세요.
  Please report vulnerabilities privately via GitHub **Security → Report a vulnerability**.
