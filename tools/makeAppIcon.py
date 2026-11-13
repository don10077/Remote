#!/usr/bin/env python3
"""Генератор иконки приложения Harmony.

Рисует тёмный глянцевый пульт (по мотивам Harmony 900), повёрнутый по
диагонали: цветной экран со списком активностей, кольцо навигации,
кнопки и ИК-волны из верхнего торца. Фон прозрачный.

Результат кладётся в app/resources/app/:
  harmony-<N>.png — PNG нужных размеров (для окна Qt и Linux);
  harmony.ico     — многоразмерная иконка для exe под Windows.

Запуск из корня проекта:  python3 tools/makeAppIcon.py
Нужен Pillow (pip install pillow).
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

# --- Палитра ----------------------------------------------------------------
BODY = (26, 29, 35)            # корпус — почти чёрный глянец
BODY_EDGE = (96, 104, 118)     # светлый кант — отделяет корпус от тёмного фона
DPAD = (58, 63, 73)            # кольцо навигации и кнопки
SCREEN_TOP = (34, 160, 180)    # экран — бирюзовый градиент, как на пульте
SCREEN_BOTTOM = (12, 62, 92)
CHIPS = [(250, 190, 60), (110, 210, 120), (240, 110, 90)]  # значки активностей
WAVES = (37, 99, 235)          # #2563eb — акцент темы Modern

SIZES = [16, 24, 32, 48, 64, 128, 256, 512]
SUPERSAMPLE = 4   # рисуем крупнее и уменьшаем — гладкие края
ANGLE = -35       # поворот пульта (по часовой стрелке), градусы


def vertical_gradient(w: int, h: int, top, bottom) -> Image.Image:
    """Прямоугольник w×h с вертикальным градиентом от top к bottom."""
    line = Image.new("RGB", (1, h))
    for y in range(h):
        t = y / max(h - 1, 1)
        line.putpixel((0, y), tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
    return line.resize((w, h))


def draw_icon(size: int) -> Image.Image:
    """Рисует иконку стороной size пикселей.

    Пульт рисуется вертикально на холсте с запасом, поворачивается и
    обрезается до квадрата — так волны над торцом не теряются при повороте.
    Для размеров ≤ 32 пульт шире, а мелкие детали не рисуются.
    """
    s = size * SUPERSAMPLE
    tiny = size <= 24
    small = size <= 32

    canvas = round(s * 1.6)
    c = canvas / 2
    layer = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)

    # --- Корпус ------------------------------------------------------------
    w = s * (0.50 if tiny else 0.42 if small else 0.31)
    h = s * (0.86 if small else 0.90)
    x0 = c - w / 2
    y0 = c - h / 2 + s * (0.10 if small else 0.09)   # сдвиг вниз — место для волн
    x1, y1 = x0 + w, y0 + h
    radius = w * 0.46
    d.rounded_rectangle((x0, y0, x1, y1), radius=radius, fill=BODY)
    # Кант: на мелких размерах толще, иначе корпус теряется на тёмном фоне.
    edge = round(SUPERSAMPLE * 0.9) if tiny else max(1, round(s * 0.007))
    d.rounded_rectangle((x0 + edge, y0 + edge, x1 - edge, y1 - edge), radius=radius - edge,
                        outline=BODY_EDGE, width=edge)

    # --- Экран -------------------------------------------------------------
    pad = w * (0.15 if small else 0.17)
    sx0, sx1 = x0 + pad, x1 - pad
    sy0 = y0 + h * (0.11 if small else 0.12)
    sy1 = sy0 + h * (0.30 if small else 0.25)
    sw, sh = round(sx1 - sx0), round(sy1 - sy0)
    screen = vertical_gradient(sw, sh, SCREEN_TOP, SCREEN_BOTTOM)
    smask = Image.new("L", (sw, sh), 0)
    ImageDraw.Draw(smask).rounded_rectangle((0, 0, sw - 1, sh - 1), radius=w * 0.07, fill=255)
    layer.paste(screen, (round(sx0), round(sy0)), smask)

    if not tiny:
        # Три строки активностей: полупрозрачная плашка и цветной значок.
        rows = 3
        row_h = sh * 0.22
        gap = (sh - rows * row_h) / (rows + 1)
        overlay = Image.new("RGBA", layer.size, (0, 0, 0, 0))
        od = ImageDraw.Draw(overlay)
        for k in range(rows):
            ry = sy0 + gap + k * (row_h + gap)
            od.rounded_rectangle((sx0 + sw * 0.09, ry, sx1 - sw * 0.09, ry + row_h),
                                 radius=row_h * 0.3, fill=(255, 255, 255, 70))
            chip = row_h * 0.72
            cy0 = ry + (row_h - chip) / 2
            od.rounded_rectangle((sx0 + sw * 0.14, cy0, sx0 + sw * 0.14 + chip, cy0 + chip),
                                 radius=chip * 0.25, fill=CHIPS[k])
            if not small:
                tx = sx0 + sw * 0.14 + chip * 1.6
                od.rounded_rectangle((tx, ry + row_h * 0.38, sx1 - sw * (0.2 + 0.1 * k),
                                      ry + row_h * 0.62), radius=row_h * 0.12,
                                     fill=(255, 255, 255, 220))
        layer.alpha_composite(overlay)

    # --- Кольцо навигации --------------------------------------------------
    cx = c
    cy = sy1 + h * (0.17 if small else 0.15)
    r = w * (0.30 if small else 0.27)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=DPAD)
    d.ellipse((cx - r * 0.42, cy - r * 0.42, cx + r * 0.42, cy + r * 0.42), fill=BODY)

    # --- Кнопки (цифровой блок) — только на крупных размерах ----------------
    if not small:
        br = w * 0.075
        top = cy + r + h * 0.07
        for row in range(4):
            by = top + row * w * 0.22
            if by + br > y1 - radius * 0.55:
                break
            for col in (-1, 0, 1):
                bx = cx + col * w * 0.26
                d.ellipse((bx - br, by - br, bx + br, by + br), fill=DPAD)

    # --- Глянцевый блик вдоль левого края -------------------------------------
    if not tiny:
        gloss = Image.new("L", layer.size, 0)
        ImageDraw.Draw(gloss).rounded_rectangle(
            (x0 + w * 0.06, y0 + h * 0.04, x0 + w * 0.16, y1 - h * 0.06),
            radius=w * 0.05, fill=60)
        gloss = gloss.filter(ImageFilter.GaussianBlur(s * 0.008))
        layer.paste(Image.new("RGBA", layer.size, (255, 255, 255, 255)), (0, 0), gloss)

    # --- ИК-волны над верхним торцом -----------------------------------------
    waves = 2 if small else 3
    wave_w = s * (0.08 if tiny else 0.05 if small else 0.032)
    step = s * (0.13 if tiny else 0.10 if small else 0.075)
    r0 = s * (0.07 if small else 0.06)
    tip_x, tip_y = c, y0 + s * 0.02
    for i in range(waves):
        rr = r0 + step * i
        d.arc((tip_x - rr, tip_y - rr, tip_x + rr, tip_y + rr), -130, -50,
              fill=WAVES, width=round(wave_w))

    # --- Поворот и подгонка под квадрат --------------------------------------
    # После поворота берём рамку непрозрачной части и вписываем её в квадрат
    # с небольшим полем: рисунок по центру и занимает всю иконку.
    layer = layer.rotate(ANGLE, resample=Image.BICUBIC, center=(c, c))
    layer = layer.crop(layer.getbbox())
    margin = 0.03 if small else 0.06
    fit = s * (1 - 2 * margin)
    k = fit / max(layer.size)
    layer = layer.resize((round(layer.width * k), round(layer.height * k)), Image.LANCZOS)
    framed = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    framed.alpha_composite(layer, ((s - layer.width) // 2, (s - layer.height) // 2))
    layer = framed

    out = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    if not small:
        shadow = layer.split()[3].filter(ImageFilter.GaussianBlur(s * 0.02)).point(lambda v: v * 0.4)
        out.paste(Image.new("RGBA", (s, s), (0, 0, 0, 255)), (round(s * 0.01), round(s * 0.025)), shadow)
    out.alpha_composite(layer)
    return out.resize((size, size), Image.LANCZOS)


def main() -> None:
    out_dir = Path(__file__).resolve().parent.parent / "app" / "resources" / "app"
    out_dir.mkdir(parents=True, exist_ok=True)

    images = {n: draw_icon(n) for n in SIZES}
    for n, im in images.items():
        im.save(out_dir / f"harmony-{n}.png", optimize=True)

    # ICO: по кадру на каждый размер до 256 (больше формат не поддерживает).
    ico_sizes = [n for n in SIZES if n <= 256]
    images[256].save(out_dir / "harmony.ico", format="ICO",
                     sizes=[(n, n) for n in ico_sizes],
                     append_images=[images[n] for n in ico_sizes if n != 256])
    print(f"готово: {out_dir}")


if __name__ == "__main__":
    main()
