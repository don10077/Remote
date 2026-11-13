"""Тёмный вариант транспорта для 1100 из серебристого mmedia.png: python tools/sprites/dark.py mmedia.png app/resources/sprites/mmedia_1100.png
(серебристый mmedia.png в репозитории больше не хранится)"""
import sys, numpy as np
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
im = np.asarray(Image.open(src).convert("RGBA")).astype(float)
rgb, a = im[..., :3], im[..., 3]
mx, mn = rgb.max(-1), rgb.min(-1)
L = rgb.mean(-1); s = (mx - mn) / np.maximum(mx, 90)  # у тёмных пикселей насыщенность не раздувается
H, W = a.shape; yy, xx = np.mgrid[0:H, 0:W]
# центральная жёлтая кнопка «пауза» и её обод остаются как есть
cx, cy, r = 480, 352, 108
keep = (xx - cx) ** 2 + (yy - cy) ** 2 < r * r
# серые пиксели кнопок: малая насыщенность, почти непрозрачные (полупрозрачное кольцо не трогаем)
w = np.clip((0.28 - s) / 0.12, 0, 1) * np.clip((a - 200) / 40, 0, 1) * (~keep)
# яркость: тёмные значки -> светло-серые, светлое серебро -> тёмное, градиент сверху вниз сохраняется
from PIL import ImageFilter
body = np.clip(18 + (L - 125) * 0.5, 14, 255)
m = np.clip((108 - L) / 22, 0, 1)
m = np.asarray(Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7))).astype(float) / 255
sym = 200  # ровный светло-серый значок
newL = body * (1 - m) + sym * m
tint = np.array([0.92, 1.0, 1.04])  # лёгкий холодный оттенок, как у 1100
new = np.clip(newL[..., None] * tint, 0, 255)
out = rgb * (1 - w[..., None]) + new * w[..., None]
Image.fromarray(np.dstack([out, a]).astype(np.uint8), "RGBA").save(dst)
