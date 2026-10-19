# P4 코어 (rosbridge · robot · maptexture · pointinstancing · msgparse · topicmodel)

목표: 요구서 (1)~(21) + 메인 세션 추가 지시(G7 하한 표, queueLength, NO_PUBLISH, mapOdomFresh 10 s, mapImageSerial, callsPending, [perf] 로그).
검사: `cmd //c "checks\\p4_core.cmd"` (test.cmd + smoke.py).

## 진행
- [x] src/msgparse.{h,cpp} (순수 함수 + robot.cpp 익명 namespace 함수 이동)
- [x] src/topicmodel.{h,cpp} (RowModel → TopicModel · SensorModel, dataChanged 만)
- [x] cmake/core.cmake (target_sources + Concurrent)
- [x] rosbridge: rxAge · 반쯤 열린 연결 abort, 이유별 throttle + 하한 + queueLength, 큰 메시지 QtConcurrent + 토픽별 순서,
      바이트 통계 · rxHistory · latencyHistory, bridgeError, call 실패 콜백 · 5 s 시간 초과 · callsPending, discover(port),
      NO_PUBLISH, shutdown, 소멸자에서 소켓 신호 끊기
- [x] robot: 신호 분리(mapImageChanged · mapPoseChanged · scanChanged), 지도 이미지 다른 스레드, mapScheme · mapBinary,
      세션 리셋, mapOdomFresh, initialpose, raw.jsonl, CSV flush · 경고, dataDir · NO_MODEL, maxSpeed WRITE, READ 속성,
      lastSendDropped, aboutToQuit, LiDAR 장착 변환, 직진 null, 모델 노출
- [x] maptexture(같은 cacheKey 무시) · pointinstancing(같은 목록 무시)
- [x] tests: tst_msgparse · tst_rosbridge · tst_robot (+ mockserver.h)
- [x] 빌드 · test.cmd(4/4) · smoke · checks\p4_core.cmd 통과

## 발견
- RosBridge 소멸 때 QWebSocket 이 disconnected 를 내며 이미 해제된 멤버를 건드려 충돌 → 소멸자에서 신호를 끊는다.
- 2000×2000 지도: 우리 코드의 GUI 시간 ≈ 0 ms, QtWebSockets 수신 처리 포함 최대 간격 22~28 ms(부하 없을 때).

## 파이에서만 확인되는 것
- 실제 /scan frame_id · 장착 각도, Foxy rosbridge status op 형식, map→odom 이 파이 rosbridge 에 오는지와 실제 수신율.
