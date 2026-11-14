#!/usr/bin/env python3
"""Генерирует спрайты экрана Harmony 900 в app/resources/sprites.

Цвета и размеры взяты из app/src/modern/remoteView.cpp (h900, LCD 232x290),
чтобы картинки совпадали с тем, что сейчас рисуется кодом.
Запуск: python tools/sprites/make900.py
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter
import math

OUT = Path(__file__).resolve().parents[2] / "app" / "resources" / "sprites"
S = 2                      # масштаб: спрайты в 2x от LCD-единиц
W, H = 232, 290            # LCD-единицы 900
HEAD, FOOT = 52, 24        # чёрные полосы шапки и подвала


def hexc(s, a=255):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4)) + (a,)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(4))


def radial_background():
    """Радиальный фон: центр (W/2, 0.55H), радиус 0.62H, стопы 0 / 0.5 / 1."""
    stops = [(0.0, hexc("#1d7087")), (0.5, hexc("#0b3442")), (1.0, hexc("#031017"))]
    img = Image.new("RGBA", (W * S, H * S))
    px = img.load()
    cx, cy, r = W / 2 * S, H * 0.55 * S, H * 0.62 * S
    for y in range(H * S):
        for x in range(W * S):
            t = min(1.0, math.hypot(x - cx, y - cy) / r)
            for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
                if t <= t1:
                    px[x, y] = lerp(c0, c1, (t - t0) / (t1 - t0))
                    break
    return img


def make_background():
    """Фон со скруглением 5, без полос: полосы шапки и подвала — отдельные спрайты."""
    img = radial_background()
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, img.width - 1, img.height - 1), 5 * S, fill=255)
    img.putalpha(mask)
    img.save(OUT / "background_900.png")


def make_band(name, height):
    Image.new("RGBA", (W * S, height * S), (0, 0, 0, 255)).save(OUT / name)


def make_tile(name, top, bottom, border, border_w=2.0, glow=None):
    """Плитка-«таблетка» 212x62 с вертикальным градиентом, рамкой и радиусом 6."""
    tw, th, k = 212, 62, 4          # k — суперсэмплинг для гладких краёв
    pad = 6 if glow else 0
    big = Image.new("RGBA", ((tw + 2 * pad) * k, (th + 2 * pad) * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    box = (pad * k, pad * k, (pad + tw) * k - 1, (pad + th) * k - 1)
    if glow:
        g = Image.new("RGBA", big.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).rounded_rectangle(box, 6 * k, outline=glow, width=6 * k)
        big = Image.alpha_composite(big, g.filter(ImageFilter.GaussianBlur(k)))
        d = ImageDraw.Draw(big)
    grad = Image.new("RGBA", (tw * k, th * k))
    gd = ImageDraw.Draw(grad)
    for y in range(th * k):
        gd.line((0, y, tw * k, y), fill=lerp(top, bottom, y / (th * k - 1)))
    mask = Image.new("L", grad.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, tw * k - 1, th * k - 1), 6 * k, fill=255)
    big.paste(grad, (pad * k, pad * k), mask)
    d.rounded_rectangle(box, 6 * k, outline=border, width=round(border_w * k))
    big.resize((big.width * S // k, big.height * S // k), Image.LANCZOS).save(OUT / name)


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    make_background()
    make_band("head_900.png", HEAD)
    make_band("foot_900.png", FOOT)
    make_tile("tile_900.png", hexc("#1f5a6c"), hexc("#0a2631"), hexc("#bcd3db"))
    make_tile("tile_900_active.png", hexc("#247d95"), hexc("#0a2b38"), hexc("#bcd3db"))
    make_tile("tile_900_selected.png", hexc("#1f5a6c"), hexc("#0a2631"), hexc("#ffffff"),
              border_w=2.5, glow=hexc("#8cc4e8", 110))
    print("ok:", sorted(p.name for p in OUT.glob("*_900*.png")))
