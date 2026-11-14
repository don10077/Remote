#!/usr/bin/env python3
"""Спрайты экрана 600/650/665/700 (ref 172x182) в app/resources/sprites.

Повторяет paintCompactScreen из remoteView.cpp: радиальный фон от screenTint
(lighter 135 / tint / darker 150), белая шапка 22, крест между ячейками, белая вкладка Devices.
Запуск: python tools/sprites/make600.py
"""
from pathlib import Path
from PIL import Image, ImageDraw
import math

OUT = Path(__file__).resolve().parents[2] / "app" / "resources" / "sprites"
W, H, S = 172, 182, 2
HEAD = 22
TINTS = {"600": "#7d7f82", "650": "#c7506d"}   # screenTint из profile.json


def rgb(s):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def lighter(c, f=135):   # как QColor::lighter (через HSV value)
    import colorsys
    h, s, v = colorsys.rgb_to_hsv(*(x / 255 for x in c))
    v2 = v * f / 100
    if v2 > 1:
        s = max(0.0, s - (v2 - 1)); v2 = 1
    return tuple(round(x * 255) for x in colorsys.hsv_to_rgb(h, s, v2))


def darker(c, f=150):
    return tuple(round(x * 100 / f) for x in c)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def background(name, tint):
    t = rgb(tint)
    stops = [(0.0, lighter(t)), (0.55, t), (1.0, darker(t))]
    img = Image.new("RGB", (W * S, H * S))
    px = img.load()
    cx, cy, r = W * 0.38 * S, H * 0.42 * S, W * 0.85 * S
    for y in range(H * S):
        for x in range(W * S):
            d = min(1.0, math.hypot(x - cx, y - cy) / r)
            for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
                if d <= t1:
                    px[x, y] = lerp(c0, c1, (d - t0) / (t1 - t0)); break
    img = img.convert("RGBA")
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, img.width - 1, img.height - 1), round(W * 0.02 * S), fill=255)
    img.putalpha(mask)
    img.save(OUT / name)


def white_head():
    Image.new("RGBA", (W * S, HEAD * S), (255, 255, 255, 255)).save(OUT / "head_600.png")


def devices_tab():
    k = 4
    w, h = 92, 20
    big = Image.new("RGBA", (w * k, h * 2 * k), (0, 0, 0, 0))
    ImageDraw.Draw(big).rounded_rectangle((0, 0, w * k - 1, h * 2 * k - 1), 6 * k, fill=(255, 255, 255, 255))
    big.crop((0, 0, w * k, h * k)).resize((w * S, h * S), Image.LANCZOS).save(OUT / "tab_600.png")


def cross():
    """Крест-разделитель (две линии, гаснущие к концам), прозрачный слой на весь экран."""
    img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    px = img.load()
    x = round((2 + 84 + 90) / 2 * S) - 0  # середина между колонками ячеек: (86+90)/2
    x = round(88 * S)
    y = round((24 + 64 + 93) / 2 * S)     # между рядами: (88+93)/2 = 90.5
    y = round(90.5 * S)
    for i in range(round(28 * S), round(H * S - 28 * S)):  # вертикаль
        t = (i - 28 * S) / (H * S - 56 * S)
        px[x, i] = (255, 255, 255, round(170 * (1 - abs(t - 0.5) * 2)))
    for i in range(round(6 * S), round(W * S - 6 * S)):    # горизонталь
        t = (i - 6 * S) / (W * S - 12 * S)
        px[i, y] = (255, 255, 255, round(170 * (1 - abs(t - 0.5) * 2)))
    img.save(OUT / "cross_600.png")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for model, tint in TINTS.items():
        background(f"background_{model}.png", tint)
    white_head(); devices_tab(); cross()
    print("ok:", sorted(p.name for p in OUT.glob("*_6[05]*.png")))
