"""방 모양(선분)과 그 위의 광선 교차 · 점유 격자(OccupancyGrid) 계산."""
import json
import math

RES = 0.05   # 지도 격자 크기(m)


class Room:
    def __init__(self, data):
        self.bounds = data["bounds"]   # [xmin, ymin, xmax, ymax] 벽 안쪽
        self.boxes = data.get("boxes", [])
        segs = [rect_segments(self.bounds)]
        segs += [rect_segments(b) for b in self.boxes]
        self.segments = [s for group in segs for s in group] + [tuple(s) for s in data.get("segments", [])]
        self._grids = {}

    @classmethod
    def load(cls, path):
        with open(path, encoding="utf-8") as f:
            return cls(json.load(f))

    def raycast(self, x, y, a0, inc, n, rmax):
        """(x, y) 에서 a0 + i*inc (i < n) 방향으로 쏜 광선이 처음 닿는 거리. rmax 밖이면 None.
        선분마다 그 선분이 가리는 각도 구간의 광선만 계산한다(1800 광선 10 Hz 를 한 코어로)."""
        best = [rmax] * n
        cos, sin, atan2, tau = math.cos, math.sin, math.atan2, 2 * math.pi
        dirs = [(cos(a0 + i * inc), sin(a0 + i * inc)) for i in range(n)]
        for ax, ay, bx, by in self.segments:
            qx, qy, ex, ey = ax - x, ay - y, bx - ax, by - ay
            if abs(qx * ey - qy * ex) < 1e-12:      # 로봇이 선분의 연장선 위
                continue
            pa, pb = atan2(qy, qx), atan2(by - y, bx - x)
            span = (pb - pa) % tau
            lo, hi = (pa, pa + span) if span <= math.pi else (pb, pb + tau - span)
            i0 = math.ceil(((lo - a0) % tau) / inc)
            cnt = int(((hi - lo) / inc)) + 2
            for k in range(i0 - 1, i0 + cnt):
                i = k % n
                dx, dy = dirs[i]
                den = dx * ey - dy * ex
                if abs(den) < 1e-12:
                    continue
                t = (qx * ey - qy * ex) / den
                if 0 < t < best[i]:
                    u = (qx * dy - qy * dx) / den
                    if 0 <= u <= 1:
                        best[i] = t
        return [b if b < rmax else None for b in best]

    def grid(self, width, height, ox, oy, oyaw):
        """방을 RES 격자로: 0 빈칸, 100 점유, -1 모름. (width, height, origin) 마다 한 번만 만든다.
        반환값은 OccupancyGrid data 의 JSON 배열 문자열(크기가 커서 미리 만들어 둔다)."""
        key = (width, height, round(ox, 6), round(oy, 6), round(oyaw, 6))
        if key in self._grids:
            return self._grids[key]
        cells = bytearray(b"\xff") * (width * height)   # 0xff = -1
        c, s = math.cos(oyaw), math.sin(oyaw)
        xmin, ymin, xmax, ymax = self.bounds
        for r in range(height):
            # 행 r 의 셀 중심: p(col) = A + col * u
            yy = (r + 0.5) * RES
            ax, ay = ox + 0.5 * RES * c - yy * s, oy + 0.5 * RES * s + yy * c
            ux, uy = RES * c, RES * s
            lo, hi = 0.0, float(width - 1)
            for a0, du, mn, mx in ((ax, ux, xmin, xmax), (ay, uy, ymin, ymax)):
                if abs(du) < 1e-12:
                    if not mn <= a0 <= mx:
                        lo, hi = 1, 0
                    continue
                t1, t2 = (mn - a0) / du, (mx - a0) / du
                lo, hi = max(lo, min(t1, t2)), min(hi, max(t1, t2))
            if lo <= hi:
                c0, c1 = math.ceil(lo), math.floor(hi)
                if c0 <= c1:
                    cells[r * width + c0:r * width + c1 + 1] = bytes(c1 - c0 + 1)
        for x1, y1, x2, y2 in self.segments:
            n = max(1, int(math.hypot(x2 - x1, y2 - y1) / (RES / 2)))
            for i in range(n + 1):
                px, py = x1 + (x2 - x1) * i / n - ox, y1 + (y2 - y1) * i / n - oy
                col, row = int((c * px + s * py) // RES), int((-s * px + c * py) // RES)
                if 0 <= col < width and 0 <= row < height:
                    cells[row * width + col] = 100
        table = {0: "0", 100: "100", 255: "-1"}
        text = "[" + ",".join(map(table.__getitem__, cells)) + "]"
        self._grids = {key: (text, cells)}   # 가장 최근 것 하나만 둔다(큰 지도 메모리)
        return text, cells


def rect_segments(b):
    x0, y0, x1, y1 = b
    return [(x0, y0, x1, y0), (x1, y0, x1, y1), (x1, y1, x0, y1), (x0, y1, x0, y0)]
