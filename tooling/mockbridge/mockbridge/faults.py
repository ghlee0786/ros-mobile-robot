"""장애 주입: 지연·흔들림·유실·끊기·멈춤·반쯤 열린 연결·폭주·시각 어긋남·깨진 JSON·시간표."""
import asyncio
import json
import random
import time


def parse_freeze(spec):
    """'S@T' -> (T, S): T 초에 S 초 동안 송신만 멈춘다."""
    secs, at = spec.split("@")
    return float(at), float(secs)


class Faults:
    def __init__(self, args, rng=None):
        self.rng = rng or random.Random(args.seed)
        self.delay = args.delay_ms / 1000.0
        self.jitter = args.jitter_ms / 1000.0
        self.drop_all, self.drop_topic = 0.0, {}
        for d in args.drop or []:
            if ":" in d:
                topic, p = d.rsplit(":", 1)
                self.drop_topic[topic] = float(p)
            else:
                self.drop_all = float(d)
        self.bad_json = args.bad_json
        self.halfopen_at = args.halfopen
        self.disconnect_every = args.disconnect_every
        self.freezes = [parse_freeze(f) for f in args.freeze or []]   # (시작, 길이) — 서버 시작 기준
        self.flood = []
        for f in args.flood or []:
            topic, hz = f.rsplit(":", 1)
            self.flood.append((topic, float(hz)))
        self.skew = args.stamp_skew_ms / 1000.0
        self.script = []
        if args.script:
            with open(args.script, encoding="utf-8") as f:
                self.script = sorted(json.load(f), key=lambda e: e["t"])
        self.t0 = time.perf_counter()

    def elapsed(self):
        return time.perf_counter() - self.t0

    def frozen(self):
        t = self.elapsed()
        return any(a <= t < a + s for a, s in self.freezes)

    def dropped(self, topic):
        p = self.drop_topic.get(topic, self.drop_all)
        return p > 0 and self.rng.random() < p

    def send_due(self):
        d = self.delay
        if self.jitter:
            d += self.rng.uniform(-self.jitter, self.jitter)
        return time.perf_counter() + max(0.0, d)

    def corrupt(self, text):
        if self.bad_json <= 0 or self.rng.random() >= self.bad_json:
            return text
        if self.rng.random() < 0.5:
            return text[: max(1, len(text) // 2)]                      # 잘린 JSON
        obj = json.loads(text)
        obj.pop(self.rng.choice(["msg", "topic"]), None)              # 필드 누락
        return json.dumps(obj)

    async def run(self, server):
        """시간에 묶인 장애(끊기 · 반쯤 열기 · 폭주 · 시간표)를 돌린다."""
        tasks = [asyncio.ensure_future(self._flood(server, topic, hz)) for topic, hz in self.flood]
        if self.halfopen_at is not None:
            tasks.append(asyncio.ensure_future(self._at(self.halfopen_at, server.halfopen)))
        if self.disconnect_every:
            tasks.append(asyncio.ensure_future(self._disconnect_loop(server)))
        for ev in self.script:
            tasks.append(asyncio.ensure_future(self._at(ev["t"], lambda ev=ev: self._script_event(server, ev))))
        await asyncio.gather(*tasks)

    async def _at(self, t, fn):
        await asyncio.sleep(max(0.0, t - self.elapsed()))
        r = fn()
        if asyncio.iscoroutine(r):
            await r

    async def _disconnect_loop(self, server):
        while True:
            await asyncio.sleep(self.disconnect_every)
            await server.disconnect_all()

    async def _flood(self, server, topic, hz):
        period, nxt = 1.0 / hz, time.perf_counter()
        while True:
            now = time.perf_counter()
            while nxt <= now:
                msg = server.source.make(topic)
                if msg is not None:
                    server.publish(topic, msg, flood=True)
                nxt += period
            if now - nxt > 1.0:
                nxt = now
            await asyncio.sleep(max(0.0, nxt - time.perf_counter()))

    def _script_event(self, server, ev):
        server.log_event("script", **ev)
        do = ev["do"]
        if do == "freeze":
            self.freezes.append((self.elapsed(), float(ev.get("secs", 1))))
        elif do == "disconnect":
            return server.disconnect_all()
        elif do == "halfopen":
            server.halfopen()
        elif do == "stop_topic":
            server.source.stopped.add(ev["topic"])
        elif do == "start_topic":
            server.source.stopped.discard(ev["topic"])
        else:
            raise ValueError(f"모르는 시간표 동작: {do}")
