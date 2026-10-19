"""record_rosbridge.py 가 남긴 JSONL 을 원래 간격(배속)대로 다시 내보낸다.

형식: 첫 줄 {"meta":{"host","started","topics":{"topics":[],"types":[]}}},
(대시보드 raw.jsonl 은 "topics":[이름…], 예전 기록은 "topics":{이름: 형식} — 모두 읽는다)
이후 줄마다 {"t":기록 시작 기준 초,"wall":epoch 초,"topic","type","msg"}.
"""
import asyncio
import json
import math
import time

from .sim import Source

LATCHED = ("/tf_static", "/map")


def read_jsonl(path):
    meta, recs = {}, []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            if "meta" in obj:
                meta = obj["meta"]
            else:
                recs.append(obj)
    return meta, recs


class Replay(Source):
    throttle_slack = 1.0   # protocol.Conn.flush_one: 기록 간격의 흔들림을 다시 거르지 않는다

    def __init__(self, server, args):
        super().__init__(server)
        self.args = args
        self.meta, self.recs = read_jsonl(args.replay)
        self.hidden = set(args.hide_topic or [])
        topics = self.meta.get("topics") or {}
        if isinstance(topics, list):
            self.types = {}
        elif "topics" in topics:
            self.types = dict(zip(topics.get("topics", []), topics.get("types", [])))
        else:
            self.types = dict(topics)
        for r in self.recs:
            self.types.setdefault(r["topic"], r.get("type", ""))
        self.last = {}
        self.latched = {}     # 래치 토픽 → {발행 키: 메시지}. /tf_static 은 발행자마다 다른 변환을 낸다
        self.skew = args.stamp_skew_ms / 1000.0

    def on_subscribe(self, conn, topic):
        for msg in self.latched.get(topic, {}).values():
            self.server.publish(topic, msg, only=conn, flood=True)

    def make(self, topic):
        return self.last.get(topic)

    def restamp(self, msg):
        t = time.time() + self.skew
        sec = math.floor(t)
        stamp = {"sec": sec, "nanosec": int((t - sec) * 1e9)}
        msg = dict(msg)
        if isinstance(msg.get("header"), dict):
            msg["header"] = {**msg["header"], "stamp": stamp}
        if isinstance(msg.get("transforms"), list):
            msg["transforms"] = [{**tr, "header": {**tr.get("header", {}), "stamp": stamp}} for tr in msg["transforms"]]
        return msg

    async def run(self):
        rate = self.args.rate
        while True:
            t0 = time.perf_counter()
            for r in self.recs:
                due = t0 + r["t"] / rate
                while (wait := due - time.perf_counter()) > 0:   # 기다리는 동안 throttle 대기분도 내보낸다
                    await asyncio.sleep(min(wait, 0.02))
                    self.server.flush()
                msg = self.restamp(r["msg"]) if self.args.restamp else r["msg"]
                self.last[r["topic"]] = msg
                if r["topic"] in LATCHED:   # 기록에 발행자가 없어 /tf_static 은 자식 프레임 묶음으로 가른다
                    key = tuple(tr.get("child_frame_id") for tr in msg.get("transforms") or [])
                    self.latched.setdefault(r["topic"], {})[key] = msg
                if r["topic"] not in self.stopped:
                    self.server.publish(r["topic"], msg)
                self.server.flush()
            if not self.args.loop:
                self.server.log_event("replay_done")
                while True:          # 서버는 계속 살아 있다(서비스 응답 · 명령 기록)
                    await asyncio.sleep(0.05)
                    self.server.flush()
            await asyncio.sleep(0.05)
