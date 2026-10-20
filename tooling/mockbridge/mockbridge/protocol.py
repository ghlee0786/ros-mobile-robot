"""rosbridge v2 JSON 프로토콜: 연결마다의 구독 상태, throttle/queue, 송신 대기열, rx/tx 로그."""
import asyncio
import collections
import itertools
import json
import logging
import math
import time

import websockets

log = logging.getLogger("mockbridge")

_raw_ids = itertools.count()


class Raw:
    """미리 만들어 둔 JSON 조각(큰 지도 data 배열). encode() 가 그대로 끼워 넣는다."""

    def __init__(self, text):
        self.text = text
        self.token = f"\x00RAW{next(_raw_ids)}\x00"


def clean(o):
    """inf/NaN 을 None(JSON null)으로 바꾼다. 실제 파이 rosbridge 가 대시보드에 보내는 모양과 같다."""
    if isinstance(o, float):
        return o if math.isfinite(o) else None
    if isinstance(o, dict):
        return {k: clean(v) for k, v in o.items()}
    if isinstance(o, (list, tuple)):
        return [clean(v) for v in o]
    return o


def encode(obj):
    raws = {}

    def default(o):
        if isinstance(o, Raw):
            raws[o.token] = o.text
            return o.token
        raise TypeError(type(o))

    text = json.dumps(clean(obj), default=default, separators=(",", ":"), ensure_ascii=False)
    for token, raw in raws.items():
        text = text.replace(json.dumps(token), raw)
    return text


def norm_type(t):
    return (t or "").replace("/msg/", "/").replace("/srv/", "/")


class Packet:
    """발행 한 건. 텍스트·CBOR 인코딩은 처음 필요할 때 한 번만 만든다."""

    def __init__(self, topic, msg, flood=False):
        self.topic, self.msg, self.flood = topic, msg, flood
        self._text = self._cbor = None

    @property
    def text(self):
        if self._text is None:
            self._text = encode({"op": "publish", "topic": self.topic, "msg": self.msg})
        return self._text

    @property
    def cbor(self):
        if self._cbor is None:
            import cbor2
            self._cbor = cbor2.dumps({"op": "publish", "topic": self.topic, "msg": json.loads(self.text)["msg"]})
        return self._cbor


class Sub:
    """한 연결의 한 토픽 구독. rosbridge 처럼 id 마다 값을 두고 throttle 은 최솟값, queue 는 최댓값을 쓴다."""

    def __init__(self, type_):
        self.type = type_
        self.params = {}             # sid -> (throttle_ms, queue_length, compression)
        self.next_due = 0.0          # 다음 발행을 보낼 수 있는 시각(perf_counter 초)
        self.pending = collections.deque(maxlen=1)

    def update(self, sid, throttle, queue, compression):
        self.params[sid] = (throttle, queue, compression)
        self._apply()

    def remove(self, sid):
        if sid is None:
            self.params.clear()
        else:
            self.params.pop(sid, None)
        if self.params:
            self._apply()

    def _apply(self):
        vals = list(self.params.values())
        self.throttle = min(v[0] for v in vals)
        self.queue = max(v[1] for v in vals)
        self.compression = "cbor" if any(v[2] == "cbor" for v in vals) else "none"
        if self.pending.maxlen != max(1, self.queue):
            self.pending = collections.deque(self.pending, maxlen=max(1, self.queue))


class Conn:
    def __init__(self, server, ws, cid):
        self.server, self.ws, self.cid = server, ws, cid
        self.subs = {}
        self.adv = {}
        self.out = asyncio.Queue()
        self.opened = time.perf_counter()
        self.halted = False

    # ---- 송신 ----------------------------------------------------------------------------
    def push(self, data, is_publish=False):
        if is_publish and self.out.qsize() > 500:   # 느린 클라이언트: 발행만 버린다
            return
        self.out.put_nowait((self.server.faults.send_due(), data))

    async def writer(self):
        faults = self.server.faults
        while True:
            due, data = await self.out.get()
            wait = due - time.perf_counter()
            if wait > 0:
                await asyncio.sleep(wait)
            while faults.frozen() or self.halted:
                await asyncio.sleep(0.02)
            await self.ws.send(data)

    def send_op(self, obj):
        self.push(encode(obj))

    def status(self, level, msg):
        self.send_op({"op": "status", "level": level, "msg": msg})

    # ---- 발행 전달(throttle · queue) ----------------------------------------------------
    def offer(self, pkt, now):
        sub = self.subs.get(pkt.topic)
        if sub is None or self.halted:
            return
        faults = self.server.faults
        if faults.frozen() or faults.dropped(pkt.topic):
            return
        if pkt.flood or sub.throttle <= 0:
            self._send(pkt, sub, now)
        else:
            sub.pending.append(pkt)
            self.flush_one(sub, now)

    def flush(self, now):
        for sub in self.subs.values():
            if sub.pending:
                self.flush_one(sub, now)

    def flush_one(self, sub, now):
        # 재생 기록은 파이 rosbridge 의 throttle 을 이미 거친 '도착' 시각이라 간격이 흔들린다. 그 흔들림을
        # 한 번 더 걸러 버리지 않도록 재생 출처는 한 주기만큼 일찍 보내도 된다(평균 빈도는 그대로 1/throttle 이하)
        slack = sub.throttle / 1000 * getattr(getattr(self.server, "source", None), "throttle_slack", 0)
        if sub.pending and now >= sub.next_due - slack:
            self._send(sub.pending.popleft(), sub, now)

    def _send(self, pkt, sub, now):
        sub.next_due = max(sub.next_due, now) + sub.throttle / 1000
        if sub.compression == "cbor":   # 실기 Foxy 는 cbor 구독에 아무 프레임도 보내지 않는다(--cbor 일 때만 보낸다)
            if not self.server.args.cbor:
                return
            data = pkt.cbor
        else:
            data = self.server.faults.corrupt(pkt.text)
        self.push(data, is_publish=True)
        self.server.log_tx(self, pkt)


class Server:
    """WebSocket 서버. source 는 sim.World 또는 replay.Replay 이고 services 는 services.Services."""

    def __init__(self, args, faults, logfile=None):
        self.args, self.faults = args, faults
        self.conns = set()
        self.source = self.services = None
        self.logf = open(logfile, "a", encoding="utf-8", buffering=1) if logfile else None
        self.cids = itertools.count(1)
        self.port = None

    # ---- 로그 ----------------------------------------------------------------------------
    def _log(self, rec):
        if self.logf:
            self.logf.write(json.dumps(clean(rec), ensure_ascii=False) + "\n")

    def log_rx(self, conn, msg):
        self._log({**msg, "t": time.perf_counter(), "wall": time.time(), "dir": "rx", "conn": conn.cid})

    def log_tx(self, conn, pkt):
        if self.args.log_tx:
            rec = {"t": time.perf_counter(), "wall": time.time(), "dir": "tx", "conn": conn.cid, "op": "publish",
                   "topic": pkt.topic}
            if len(pkt.text) < 4096:
                rec["msg"] = pkt.msg
            self._log(rec)

    def log_event(self, what, **kw):
        self._log({"t": time.perf_counter(), "wall": time.time(), "dir": "event", "op": what, **kw})

    # ---- 발행 -----------------------------------------------------------------------------
    def wants(self, topic):
        return any(topic in c.subs for c in self.conns)

    def publish(self, topic, msg, flood=False, only=None):
        pkt = Packet(topic, msg, flood)
        now = time.perf_counter()
        for c in ([only] if only else list(self.conns)):
            c.offer(pkt, now)

    def flush(self):
        now = time.perf_counter()
        for c in list(self.conns):
            c.flush(now)

    # ---- 받기 ----------------------------------------------------------------------------
    async def handler(self, ws, *_):
        conn = Conn(self, ws, next(self.cids))
        self.conns.add(conn)
        self.faults.connected()
        self.log_event("connect", conn=conn.cid)
        writer = asyncio.ensure_future(conn.writer())
        try:
            async for raw in ws:
                if isinstance(raw, bytes):
                    raw = raw.decode("utf-8", "replace")
                try:
                    msg = json.loads(raw)
                except ValueError:
                    conn.status("error", "JSON 을 읽을 수 없음")
                    continue
                if not isinstance(msg, dict):
                    continue
                self.log_rx(conn, msg)
                try:
                    self.dispatch(conn, msg)
                except Exception as e:   # 한 메시지의 오류로 연결을 끊지 않는다
                    log.exception("op 처리 실패")
                    conn.status("error", f"{msg.get('op')}: {e}")
        except websockets.ConnectionClosed:
            pass
        finally:
            writer.cancel()
            self.conns.discard(conn)
            self.log_event("disconnect", conn=conn.cid)

    def dispatch(self, conn, msg):
        op = msg.get("op")
        if op == "advertise":
            conn.adv[msg["topic"]] = norm_type(msg.get("type"))
        elif op == "unadvertise":
            conn.adv.pop(msg.get("topic"), None)
        elif op == "subscribe":
            self.subscribe(conn, msg)
        elif op == "unsubscribe":
            sub = conn.subs.get(msg.get("topic"))
            if sub:
                sub.remove(msg.get("id"))
                if not sub.params:
                    del conn.subs[msg["topic"]]
        elif op == "publish":
            topic = msg.get("topic")
            if topic not in conn.adv:   # rosbridge 는 advertise 없는 publish 도 형식을 알면 받는다
                t = self.source.topic_type(topic)
                if not t:
                    conn.status("error", f"publish {topic}: advertise 되지 않았고 형식을 알 수 없음")
                    return
                conn.adv[topic] = t
            self.source.on_command(topic, msg.get("msg") or {})
        elif op == "call_service":
            asyncio.ensure_future(self.call(conn, msg))
        else:
            conn.status("error", f"모르는 op: {op}")

    def subscribe(self, conn, msg):
        topic, want = msg.get("topic"), norm_type(msg.get("type"))
        known = norm_type(self.source.topic_type(topic))
        # Foxy rosbridge 는 모르는 형식 · 형식 불일치 구독에 status 를 보내지 않고 데이터도 보내지 않는다(실기 확인)
        if want and (want not in self.source.known_types() or (known and want != known)):
            log.warning("subscribe %s: 형식 %s 무시(알려진 형식 %s)", topic, want, known or "-")
            return
        if not want and not known:
            conn.status("error", f"subscribe {topic}: 아직 발행되지 않아 형식을 알 수 없음")
            return
        comp = msg.get("compression") or "none"
        if comp not in ("none", "cbor") or (comp == "cbor" and not self.args.cbor):
            log.warning("compression %s 요청 무시(%s) → none", comp, topic)
        if msg.get("fragment_size"):
            log.warning("fragment_size 요청 무시(%s)", topic)
        throttle = int(msg.get("throttle_rate") or 0)
        queue = int(msg.get("queue_length") if msg.get("queue_length") is not None else 0)
        if throttle > 0:
            queue = max(1, queue)
        new = topic not in conn.subs
        if new:
            conn.subs[topic] = Sub(want or known)
        conn.subs[topic].update(msg.get("id"), throttle, queue, comp)
        if new:
            self.source.on_subscribe(conn, topic)

    async def call(self, conn, msg):
        service = msg.get("service", "")
        res = self.services.handle(service, msg.get("args") or {})
        if res is None:      # --no-respond
            return
        result, values = res
        conn.send_op({"op": "service_response", "service": service, "id": msg.get("id"),
                      "result": result, "values": values})

    # ---- 실행 -----------------------------------------------------------------------------
    async def disconnect_all(self):
        for c in list(self.conns):
            self.log_event("server_close", conn=c.cid)
            c.ws.transport.abort()

    def halfopen(self):
        """지금 열린 연결의 읽기·쓰기를 모두 멈춘다(pong 도 멈춘다). 소켓은 닫지 않는다."""
        for c in list(self.conns):
            c.halted = True
            c.ws.transport.pause_reading()
            self.log_event("halfopen", conn=c.cid)

    async def serve(self, host, port):
        ping = None if self.faults.halfopen_at is not None else 20
        srv = await websockets.serve(self.handler, host, port, max_size=None, ping_interval=ping,
                                     compression=None)
        self.port = srv.sockets[0].getsockname()[1]
        return srv
