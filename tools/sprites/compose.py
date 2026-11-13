"""Пробная сборка экранов Harmony 1000/1100 из app/resources/sprites (960x720), для проверки глазами.
python tools/sprites/compose.py app/resources <папка вне репозитория>"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

RES = Path(sys.argv[1]); OUT = Path(sys.argv[2]); SP = RES / "sprites"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
W, H = 960, 720
ACTIVE = {"1000": "#8BAD2D", "1100": "#FAA72C"}

def sp(name): return Image.open(SP / f"{name}.png").convert("RGBA")
def tint(im, color):
    solid = Image.new("RGBA", im.size, color); solid.putalpha(im.getchannel("A")); return solid
def font(size): return ImageFont.truetype(FONT, size)
def text(d, xy, s, size, fill, anchor="mm"): d.text(xy, s, font=font(size), fill=fill, anchor=anchor)
def paste(c, im, xy): c.alpha_composite(im, (int(xy[0]), int(xy[1])))
def center(c, im, cx, cy): paste(c, im, (cx - im.width / 2, cy - im.height / 2))

def tile(c, m, x, y, size=None):
    """Плитка активности; size — уменьшенная копия (избранное). У 1000 под ней мягкая тень вправо-вниз, как на фото."""
    t = sp(f"tile_{m}")
    k = 1.0
    if size:
        k = size[0] / t.width; t = t.resize(size, Image.LANCZOS)
    if m == "1000":
        p = round(12 * k)
        sh = Image.new("RGBA", (t.width + 2 * p, t.height + 2 * p), (0, 0, 0, 0))
        sh.paste((0, 20, 50, 150), (p, p, p + t.width, p + t.height), t.getchannel("A"))
        paste(c, sh.filter(ImageFilter.GaussianBlur(4 * k)), (x - round(9 * k), y - round(8 * k)))
    paste(c, t, (x, y))

def base(m):
    c = Image.new("RGBA", (W, H), "black"); paste(c, sp(f"background_{m}"), (0, 0)); return c
def footer(c, m):
    f = sp(f"footer_{m}"); paste(c, f, (0, H - f.height))
def battery(c, level=3):
    """Батарея 1100 в верхней планке слева от Help; level — сколько делений energy (0..3)."""
    b, e = sp("battery"), sp("energy"); x, y = 300, 6
    paste(c, b, (x, y))
    for i in range(level): paste(c, e, (x + 3 + i * 14, y + 4))

def corners_1100(c):
    paste(c, sp("topleft_1100"), (0, 0)); paste(c, sp("topright_1100"), (W - 100, 0))
    paste(c, sp("bottomleft_1100"), (0, H - 89)); paste(c, sp("bottomright_1100"), (W - 100, H - 89)); battery(c)

def corner_icons(c, m, tl, tr, bl, br, active=None):
    pos = {"1000": [(45, 42), (W - 45, 42), (45, H - 40), (W - 45, H - 40)],
           "1100": [(50, 44), (W - 50, 44), (50, H - 44), (W - 50, H - 44)]}[m]
    for name, (x, y) in zip([tl, tr, bl, br], pos):
        if not name: continue
        im = sp(name)
        if name == active: im = tint(im, ACTIVE[m])
        center(c, im, x, y)

def panels(c, m, left, right, lsprite=None):
    if m == "1000":
        l = sp(lsprite or "buttonleft_full_1000"); r = sp("buttonright_1000")
        y0, slots, col, size = 132, [55, 170, 290, 405], (60, 60, 60), 30
    else:
        l, r = sp("buttonleft_1100"), sp("buttonright_1100")
        y0, slots, col, size = 104, [63, 190, 316, 443], (255, 255, 255), 30
    rx = W - r.width
    paste(c, l, (0, y0)); paste(c, r, (rx, y0)); d = ImageDraw.Draw(c)
    for i, s in enumerate(left):
        if s: text(d, (l.width / 2 - 8, y0 + slots[i]), s, size, col)
    for i, s in enumerate(right):
        if s: text(d, (rx + r.width / 2 + 8, y0 + slots[i]), s, size, col)
    return l, r, y0

def title(c, m, s):
    text(ImageDraw.Draw(c), (W / 2, 678 if m == "1000" else 694), s, 38 if m == "1000" else 30, "white")

def keypad(m):
    c = base(m); footer(c, m)
    if m == "1100": corners_1100(c)
    panels(c, m, ["Back", "LiveTV", "", "Aspect"], ["", "Guide", "Info", "Exit"])
    if m == "1000":
        paste(c, sp("digital_1000"), (230, 92))
        corner_icons(c, m, "play", "heart", "number", "favorites", active="number")
    else:
        paste(c, sp("digital_1100"), (233, 100))
        corner_icons(c, m, "play", "heart", "number2", "favorites", active="number2")
    title(c, m, "Watch TV"); return c

def transport(m):
    c = base(m); footer(c, m)
    if m == "1100": corners_1100(c)
    if m == "1000":
        panels(c, m, ["Live TV", "", "", ""], ["Menu", "Guide", "Info", "Exit"])
        corner_icons(c, m, "play", "heart", "number", "favorites", active="play")
    else:
        panels(c, m, ["", "", "", ""], ["", "Menu", "Exit", "Info"])
        corner_icons(c, m, "play", "cd", "number2", "favorites", active="play")
    paste(c, sp(f"mmedia_{m}"), (0, 0))
    title(c, m, "Watch TV" if m == "1000" else "Watch a DVD"); return c

ACTS = [("watch_tv", "Watch TV"), ("watch_dvd", "Watch a DVD"), ("listen_to_music", "Listen to Music"),
        ("watch_vcr", "Watch VCR"), ("listen_to_radio", "Listen to Radio"), ("play_a_game", "Play Game")]

def activities(m):
    c = base(m); d = ImageDraw.Draw(c)  # у 1000 на стартовом экране футера нет
    if m == "1100": footer(c, m)
    if m == "1000":
        xs, ys, tw, th = [100, 365, 628], [168, 424], 230, 227
        text(d, (W / 2, 120), "What would you like to do?", 36, "white")
        corner_icons(c, m, "setting_1000", "home", None, None, active="home")
    else:
        corners_1100(c); xs, ys, tw, th = [110, 366, 624], [110, 387], 231, 229
        corner_icons(c, m, "home", "setting_1100", None, None, active="home")
    for i, (icon, label) in enumerate(ACTS):
        x, y = xs[i % 3], ys[i // 3]
        tile(c, m, x, y)
        im = Image.open(RES / "activity_icons" / f"{icon}.png").convert("RGBA")
        center(c, im.resize((im.width * 2, im.height * 2), Image.LANCZOS), x + tw / 2, y + 85)
        text(ImageDraw.Draw(c), (x + tw / 2, y + th - 40), label, 24, "white" if m == "1100" else (40, 40, 40))
    text(ImageDraw.Draw(c), (W / 2, 686 if m == "1000" else 690), "11:24 AM", 32, "white"); return c

def command(m):
    """Избранное: у обеих моделей сетка 3x2 по фото 1100, плитки — уменьшенные tile_<модель>."""
    c = base(m); footer(c, m)
    if m == "1100": corners_1100(c)
    xs, ys, tw, th = [193, 388, 583], [112, 308], 183, 180
    if m == "1000":
        # у 1000 листание — стрелки Page на левой панели, надписи «1 of 3» нет
        l, r, y0 = panels(c, m, ["Guide", "", "", ""], ["Live TV", "Display", "Info", "Clear"], lsprite="buttonleft_1000")
        center(c, sp("arrow_1000"), l.width / 2 - 8, y0 + 290)
        text(ImageDraw.Draw(c), (l.width / 2 - 8, y0 + 290), "Page", 30, (60, 60, 60))
    else:
        panels(c, m, ["", "Guide", "", ""], ["Live TV", "Display", "Info", "Clear"])
    corner_icons(c, m, "play", "heart", "number2" if m == "1100" else "number", "favorites", active="heart")
    for i, ch in enumerate(["FOX", "SPEED", "SUN", "NGC", "FX", "Soccer"]):
        x, y = xs[i % 3], ys[i // 3]
        tile(c, m, x, y, (tw, th))
        text(ImageDraw.Draw(c), (x + tw / 2, y + th / 2), ch, 28, "white" if m == "1100" else (40, 40, 40))
    if m == "1100":
        p = sp("pagination"); paste(c, p, ((W - p.width) / 2, H - p.height))
        text(ImageDraw.Draw(c), (W / 2, H - p.height / 2), "1 of 3", 30, "white")
    return c

OUT.mkdir(parents=True, exist_ok=True)
for m in ["1000", "1100"]:
    for fn in [activities, command, transport, keypad]:
        fn(m).convert("RGB").save(OUT / f"{m}_{fn.__name__}.png")
print("ok")
