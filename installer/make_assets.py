"""설치 마법사 그림을 만든다(Pillow): installer/assets/*.bmp

- wizard.bmp       : 환영 · 완료 화면 왼쪽 세로 그림(앱 아이콘 dashboard/icons/app.png ← app.svg)
- wizard_small.bmp : 각 페이지 오른쪽 위 작은 아이콘
- features_ko.bmp / features_en.bmp : '제품 특징' 페이지 그림(기능 8칸)

다시 만들 때: uv run --no-project --with pillow python installer/make_assets.py
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "installer" / "assets"
ICON = Image.open(ROOT / "dashboard" / "icons" / "app.png").convert("RGBA")
FONT = ROOT / "dashboard" / "fonts" / "PretendardVariable.ttf"

# qml/Theme.qml 의 토큰과 같은 색
CANVAS, ACCENT_SUBTLE, ACCENT = "#0B0D10", "#1A2C4A", "#2F6FE4"
TEXT1, TEXT2 = "#16181D", "#525A66"
SERIES = ["#4C9AFF", "#F2994A", "#56CCB2", "#BB86FC", "#F2C94C", "#E5534B", "#57AB5A"]


def font(size, weight="Regular"):
    f = ImageFont.truetype(str(FONT), size)
    try:
        f.set_variation_by_name(weight)
    except (OSError, ValueError):
        pass
    return f


def gradient(w, h, top, bottom):
    img = Image.new("RGB", (w, h))
    a, b = Image.new("RGB", (1, 1), top).getpixel((0, 0)), Image.new("RGB", (1, 1), bottom).getpixel((0, 0))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / (h - 1)
        d.line([(0, y), (w, y)], fill=tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3)))
    return img


def wizard():
    w, h = 328, 628   # 200 % 크기(100 % 는 164 x 314) — Inno 가 맞춰 줄인다
    img = gradient(w, h, ACCENT_SUBTLE, CANVAS)
    icon = ICON.resize((176, 176), Image.LANCZOS)
    img.paste(icon, ((w - 176) // 2, 150), icon)
    d = ImageDraw.Draw(img)
    for text, y, f, fill in (("로봇 대시보드", 360, font(34, "Bold"), "#E8EAED"),
                             ("Robot Dashboard", 408, font(20, "Medium"), "#A3A9B2"),
                             ("ROS 2 · Qt 6 Quick 3D", 560, font(16), "#7F8792")):
        d.text((w // 2, y), text, font=f, fill=fill, anchor="mm")
    img.save(OUT / "wizard.bmp")


def wizard_small():
    img = Image.new("RGB", (110, 110), "white")
    icon = ICON.resize((100, 100), Image.LANCZOS)
    img.paste(icon, (5, 5), icon)
    img.save(OUT / "wizard_small.bmp")


# 기능 칸 그림(선 아이콘): 칸 왼쪽 위 (x, y) 에 64 px 안에 그린다
def pict(d, kind, x, y, c):
    s = 4
    if kind == "cube":
        top = [(x + 32, y + 6), (x + 58, y + 20), (x + 32, y + 34), (x + 6, y + 20)]
        d.polygon(top, outline=c, width=s)
        d.line([(x + 6, y + 20), (x + 6, y + 46), (x + 32, y + 60), (x + 58, y + 46), (x + 58, y + 20)], fill=c, width=s)
        d.line([(x + 32, y + 34), (x + 32, y + 60)], fill=c, width=s)
    elif kind == "goal":
        d.ellipse([x + 6, y + 6, x + 58, y + 58], outline=c, width=s)
        d.ellipse([x + 24, y + 24, x + 40, y + 40], fill=c)
        d.line([(x + 32, y + 0), (x + 32, y + 14)], fill=c, width=s)
        d.line([(x + 50, y + 32), (x + 64, y + 32)], fill=c, width=s)
    elif kind == "map":
        for i in range(4):
            for j in range(4):
                shade = [c, "#D0D5DC", c, "#9AA3AE"][(i * 3 + j) % 4]
                d.rounded_rectangle([x + 4 + i * 15, y + 4 + j * 15, x + 16 + i * 15, y + 16 + j * 15], 3, fill=shade)
    elif kind == "graph":
        d.line([(x + 4, y + 4), (x + 4, y + 60), (x + 62, y + 60)], fill="#9AA3AE", width=3)
        d.line([(x + 6, y + 48), (x + 18, y + 30), (x + 30, y + 40), (x + 44, y + 14), (x + 60, y + 24)], fill=c, width=s, joint="curve")
    elif kind == "search":
        d.ellipse([x + 4, y + 4, x + 44, y + 44], outline=c, width=s + 1)
        d.line([(x + 38, y + 38), (x + 60, y + 60)], fill=c, width=s + 3)
    elif kind == "safety":
        pts = [(x + 32 + 29 * math.cos(math.radians(22.5 + 45 * k)), y + 32 + 29 * math.sin(math.radians(22.5 + 45 * k)))
               for k in range(8)]
        d.polygon(pts, fill=c)
        d.line([(x + 32, y + 16), (x + 32, y + 38)], fill="white", width=6)
        d.ellipse([x + 28, y + 43, x + 36, y + 51], fill="white")
    elif kind == "mock":
        d.rounded_rectangle([x + 4, y + 8, x + 60, y + 56], 8, outline=c, width=s)
        d.polygon([(x + 26, y + 20), (x + 26, y + 44), (x + 46, y + 32)], fill=c)


FEATURES = {
    "ko": [
        ("cube", "3D 뷰", "로봇 모델 · LiDAR 점 · TF 축을 실시간 3D 로"),
        ("goal", "Nav2 클릭 목표", "지도를 누르고 끌어 목표 · 도착 방향 · 초기 위치"),
        ("map", "지도 · 코스트맵", "RViz2 와 같은 색 규칙으로 지도 · 전역/지역 코스트맵"),
        ("graph", "시간 그래프", "PlotJuggler 식 — 아무 토픽 필드나 그래프로"),
        ("search", "데이터 점검", "토픽 · 로그 · 진단 · 노드 · 서비스 · 파라미터"),
        ("safety", "안전", "워치독 · 비상 정지 · 조종 잠금이 기본"),
        ("mock", "모의 로봇으로 시험", "로봇 없이 모의 rosbridge 로 화면과 동작 확인"),
    ],
    "en": [
        ("cube", "3D view", "Real robot model, LiDAR points and TF axes in live 3D"),
        ("goal", "Nav2 click goals", "Press and drag on the map for goal, heading, initial pose"),
        ("map", "Map · costmaps", "Map and global/local costmaps in RViz2 colours"),
        ("graph", "Time plots", "PlotJuggler-style plots of any topic field"),
        ("search", "Data inspector", "Topics, logs, diagnostics, nodes, services, params"),
        ("safety", "Safety", "Watchdog, emergency stop and drive lock built in"),
        ("mock", "Mock robot", "Try every screen without hardware via a mock rosbridge"),
    ],
}
LAST = {"ko": ("로봇 대시보드", "ROS 2 모바일 로봇을 위한 Windows 앱"),
        "en": ("Robot Dashboard", "A Windows app for ROS 2 mobile robots")}


def wrap(d, text, f, width):
    lines, cur = [], ""
    for word in text.split(" "):
        t = (cur + " " + word).strip()
        if d.textlength(t, font=f) <= width or not cur:
            cur = t
        else:
            lines.append(cur)
            cur = word
    return lines + [cur]


def features(lang):
    w, h, cols, gap = 1000, 570, 4, 16   # 마법사 안쪽 페이지(약 500 x 285)의 2 배
    tw, th = (w - gap * (cols + 1)) // cols, (h - gap * 3) // 2
    img = Image.new("RGB", (w, h), "white")
    d = ImageDraw.Draw(img)
    ft, fd = font(25, "Bold"), font(19)
    items = FEATURES[lang] + [None]
    for n, item in enumerate(items):
        x, y = gap + (n % cols) * (tw + gap), gap + (n // cols) * (th + gap)
        if item is None:
            d.rounded_rectangle([x, y, x + tw, y + th], 18, fill=ACCENT_SUBTLE)
            icon = ICON.resize((96, 96), Image.LANCZOS)
            img.paste(icon, (x + 22, y + 22), icon)
            title, desc = LAST[lang]
            d.text((x + 22, y + 138), title, font=ft, fill="#E8EAED")
            for k, line in enumerate(wrap(d, desc, fd, tw - 44)):
                d.text((x + 22, y + 178 + k * 27), line, font=fd, fill="#A3A9B2")
            continue
        kind, title, desc = item
        c = SERIES[n % len(SERIES)]
        d.rounded_rectangle([x, y, x + tw, y + th], 18, fill="#F0F2F5")
        pict(d, kind, x + 22, y + 26, c)
        d.text((x + 22, y + 120), title, font=ft, fill=TEXT1)
        for k, line in enumerate(wrap(d, desc, fd, tw - 44)):
            d.text((x + 22, y + 160 + k * 27), line, font=fd, fill=TEXT2)
    img.save(OUT / f"features_{lang}.bmp")


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    wizard()
    wizard_small()
    for lang in FEATURES:
        features(lang)
    print("installer/assets 에 그림을 만들었다")
