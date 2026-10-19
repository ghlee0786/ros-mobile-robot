"""record_rosbridge.py 가 남긴 JSONL 을 원래 간격(배속)대로 다시 내보낸다.

형식: 첫 줄 {"meta":{"host","started","topics":{"topics":[],"types":[]}}},
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
    def __init__(self, server, args):
        super().__init__(server)
        self.args = args
        self.meta, self.recs = read_jsonl(args.replay)
        self.hidden = set(args.hide_topic or [])
        topics = self.meta.get("topics") or {}
        self.types = dict(zip(topics.get("topics", []), topics.get("types", [])))
        for r in self.recs:
            self.types.setdefault(r["topic"], r.get("type", ""))
        self.last = {}
        self.skew = args.stamp_skew_ms / 1000.0

    def on_subscribe(self, conn, topic):
        if topic in LATCHED and topic in self.last:
            self.server.publish(topic, self.last[topic], only=conn)

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
                if r["topic"] not in self.stopped:
                    self.server.publish(r["topic"], msg)
                self.server.flush()
            if not self.args.loop:
                self.server.log_event("replay_done")
                while True:          # 서버는 계속 살아 있다(서비스 응답 · 명령 기록)
                    await asyncio.sleep(0.05)
                    self.server.flush()
            await asyncio.sleep(0.05)
