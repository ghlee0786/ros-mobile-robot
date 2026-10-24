"""라즈베리 파이 rosbridge(:9090, JSON v2)를 흉내 내는 모의 서버.

대시보드를 파이 없이 시험한다. 차동 구동 시뮬레이션·워치독·직진·경로 추종·핑퐁·지도·nav 토픽을 내고,
rosapi 와 가짜 노드 매개변수 서비스에 답하며, 장애(지연·유실·끊김·멈춤·반쯤 열린 연결)를 넣을 수 있다.

    UV_CACHE_DIR=/e/Temp/uv-cache uv run --no-project --with "websockets>=12,<14" \\
        python tooling/mockbridge/mock_rosbridge.py --port 9091 [옵션]

예)
    --port 0 --log E:/Temp/rx.jsonl          빈 포트로 띄우고 받은 op 를 기록(시작하면 'MOCKBRIDGE_PORT=<n>' 출력)
    --nav --map-odom 1,2,30                   Nav2 토픽과 map→odom 변환을 함께 낸다
    --nav --nav-fail plan                     /goal_pose 를 받으면 /nav/status 로 계획 실패와 reason 을 낸다
    --freeze 3@10 --drop /scan:0.1            첫 연결 10 초 뒤 3 초 송신 멈춤, /scan 10% 유실
    --replay run1.jsonl --rate 2 --loop --restamp   기록을 2 배속으로 반복 재생(시각은 지금으로)
"""
import argparse
import asyncio
import json
import logging
import os

from mockbridge.faults import Faults
from mockbridge.protocol import Server
from mockbridge.room import Room
from mockbridge.services import Services

HERE = os.path.dirname(os.path.abspath(__file__))


def parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = p.add_argument_group("서버")
    g.add_argument("--bind", default="127.0.0.1", help="듣는 주소 (기본 127.0.0.1)")
    g.add_argument("--port", type=int, default=9091, help="포트, 0 이면 빈 포트 (기본 9091)")
    g.add_argument("--log", help="받은 op 를 한 줄씩 남길 JSONL")
    g.add_argument("--log-tx", action="store_true", help="보낸 publish 도 --log 에 남긴다")
    g.add_argument("--cbor", action="store_true", help="compression:cbor 구독에 CBOR 바이너리로 보낸다(cbor2 필요)")
    g.add_argument("--seed", type=int, default=1, help="난수 씨앗")
    g.add_argument("--host-name", default="mockpi", help="하트비트·pong 에 넣을 호스트 이름")
    g.add_argument("--hide-topic", action="append", help="/rosapi/topics 에서 숨길 토픽(여러 번)")
    g.add_argument("--no-respond", action="append", help="응답하지 않을 서비스(여러 번)")
    g.add_argument("--clients", type=int, help="/client_count 값을 고정")
    g.add_argument("--foxy-param-bug", action="store_true",
                   help="실기 Foxy rclpy 처럼 describe_parameters 의 모든 descriptor 에 마지막 파라미터의 이름 · 형식을 넣는다")

    g = p.add_argument_group("시뮬레이션")
    g.add_argument("--room", default=os.path.join(HERE, "rooms", "default.json"), help="방 JSON")
    g.add_argument("--no-watchdog", action="store_true", help="cmd_watchdog 노드가 없는 것처럼(/teleop/cmd_vel 무시)")
    g.add_argument("--left-gain", type=float, default=0.95, help="왼쪽 바퀴가 지령의 몇 배로 도는지")
    g.add_argument("--encoder-scale", type=float, default=1.01, help="오른쪽 엔코더 배율")
    g.add_argument("--imu-offset", type=float, default=1.0, help="IMU yaw 고정 오프셋(rad)")
    g.add_argument("--imu-noise", type=float, default=0.002, help="IMU yaw 잡음(rad)")
    g.add_argument("--odom-cov", action="store_true", help="/odom pose.covariance 를 0 이 아니게")
    g.add_argument("--map-odom", help="map→odom 'x,y,yaw_deg' (5 Hz /tf)")
    g.add_argument("--laser-yaw-deg", type=float, default=0.0, help="base_link→laser yaw")
    g.add_argument("--scan-frame", default="base_link",
                   help="/scan frame_id (실기는 base_link: 로봇 중심에서 잰다. 다른 값이면 laser 장착 위치에서 잰다)")
    g.add_argument("--pi-initialpose", action="store_true",
                   help="파이 initialpose_node 처럼 5 s 마다 /initialpose(map 원점)를 발행하고 적용한다")
    g.add_argument("--no-scan-compact", action="store_true",
                   help="/scan_compact(UInt16MultiArray, 거리 mm)를 내지 않는다(/rosapi/topics 에서도 빠진다)")
    g.add_argument("--scan-rays", type=int, default=1800, help="/scan 광선 수")
    g.add_argument("--scan-rays-from", help="기록 JSONL 의 첫 /scan 광선 수로 맞춘다")
    g.add_argument("--scan-max", type=float, default=25.0, help="/scan range_max(m)")
    g.add_argument("--scan-noise", type=float, default=0.0, help="/scan 거리 잡음(m)")
    g.add_argument("--scan-dropout", type=float, default=0.0, help="/scan 광선 유실 확률")
    g.add_argument("--self-hit", action="store_true", help="몸체에 맞은 근거리 반사를 섞는다")
    g.add_argument("--map-every", type=float, default=0.0, help="/map 을 N 초마다 다시 발행")
    g.add_argument("--map-size", help="지도 격자 WxH (예: 2000x2000)")
    g.add_argument("--map-max", type=int, default=2000, help="--map-size 한 변 상한(4000 은 약 50MB JSON)")
    g.add_argument("--map-grow", action="store_true", help="2 s 마다 지도 너비를 늘리고 origin 을 음수 쪽으로")
    g.add_argument("--map-origin-yaw-deg", type=float, default=0.0, help="지도 origin yaw")
    g.add_argument("--plan-frame", choices=["map", "odom"], default="map", help="/plan 프레임")
    g.add_argument("--null-heading", action="store_true", help="직진 status 의 heading_err_deg 를 null 로")
    g.add_argument("--rosout-burst", type=float, default=0.0, help="/rosout 을 N Hz 로 쏟아 낸다")
    g.add_argument("--diag-stale", type=float, default=0.0, help="/diagnostics 를 첫 연결 N 초 뒤 멈춤")
    g.add_argument("--nav", action="store_true", help="amcl·costmap·footprint·액션 상태·/nav/status 토픽을 낸다")
    g.add_argument("--nav-fail", choices=["plan"], help="--nav 의 /goal_pose 를 실패시킨다(plan: 출발점 막힘으로 계획 실패)")

    g = p.add_argument_group("장애 주입")
    g.add_argument("--delay-ms", type=float, default=0.0, help="송신 지연")
    g.add_argument("--jitter-ms", type=float, default=0.0, help="송신 지연 흔들림(±)")
    g.add_argument("--drop", action="append", help="유실 확률 P 또는 topic:P (여러 번)")
    g.add_argument("--disconnect-every", type=float, default=0.0, help="S 초마다 모든 연결을 닫는다")
    g.add_argument("--freeze", action="append", help="S@T: 첫 연결 T 초 뒤 S 초 동안 송신만 멈춤(여러 번)")
    g.add_argument("--halfopen", type=float, help="첫 연결 T 초 뒤 열린 연결의 읽기·쓰기를 멈춘다(pong 없음)")
    g.add_argument("--flood", action="append", help="topic:Hz throttle 무시하고 쏟아 낸다(여러 번)")
    g.add_argument("--stamp-skew-ms", type=float, default=0.0, help="header.stamp 를 이만큼 어긋나게")
    g.add_argument("--bad-json", type=float, default=0.0, help="발행 중 깨진 JSON·필드 누락 확률")
    g.add_argument("--script", help='시간표 JSON [{"t":10,"do":"freeze","secs":3},{"t":20,"do":"disconnect"},'
                                    '{"t":25,"do":"stop_topic","topic":"/scan"}]')
    g.add_argument("--freeze-pose", action="store_true", help="로봇을 움직이지 않고 잡음도 끈다(결정적 장면)")

    g = p.add_argument_group("재생")
    g.add_argument("--replay", help="record_rosbridge.py JSONL 을 메시지 출처로")
    g.add_argument("--rate", type=float, default=1.0, help="재생 배속")
    g.add_argument("--loop", action="store_true", help="끝나면 처음부터 다시")
    g.add_argument("--restamp", action="store_true", help="header.stamp·tf stamp 를 지금 시각으로")
    return p


def scan_rays_from(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            rec = json.loads(line)
            if rec.get("topic") == "/scan":
                return len(rec["msg"]["ranges"])
    raise SystemExit(f"{path} 에 /scan 이 없음")


async def main(args):
    faults = Faults(args)
    server = Server(args, faults, args.log)
    server.services = Services(server, args)
    if args.replay:
        from mockbridge.replay import Replay
        server.source = Replay(server, args)
    else:
        from mockbridge.sim import World
        if args.scan_rays_from:
            args.scan_rays = scan_rays_from(args.scan_rays_from)
        server.source = World(server, args, Room.load(args.room))
    srv = await server.serve(args.bind, args.port)
    print(f"MOCKBRIDGE_PORT={server.port}", flush=True)
    logging.info("ws://%s:%d 에서 대기 (%s)", args.bind, server.port, "재생" if args.replay else "시뮬레이션")
    try:
        await asyncio.gather(server.source.run(), faults.run(server))
    finally:
        srv.close()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    try:
        asyncio.run(main(parser().parse_args()))
    except KeyboardInterrupt:
        pass
