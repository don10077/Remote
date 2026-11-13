"""Плитка активности из фото экрана: содержимое (иконка, подпись) заменяется
интерполяцией Кунса по чистой рамке вокруг него, углы скругляются маской.
python tools/sprites/tile.py <папка с фото t1000_activities.png и t1100_activities.png> app/resources/sprites
(фото экранов в репозитории больше не хранятся; плитки уже нарезаны)"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

def smooth1d(a, k=5):
    pad = np.pad(a, ((k, k), (0, 0)), mode="edge")
    ker = np.ones(2 * k + 1) / (2 * k + 1)
    return np.stack([np.convolve(pad[:, c], ker, "valid") for c in range(a.shape[1])], 1)

def linfit(a):
    x = np.arange(len(a))
    return np.stack([np.polyval(np.polyfit(x, a[:, c], 1), x) for c in range(a.shape[1])], 1)

def make(src, box, inner, radius, out):
    x0, y0, x1, y1 = box                      # граница плитки (полупиксели допустимы)
    im = np.asarray(Image.open(src).convert("RGB"), dtype=float)
    X0, Y0, X1, Y1 = int(x0), int(y0), int(np.ceil(x1)), int(np.ceil(y1))
    t = im[Y0:Y1, X0:X1].copy()
    a, b, c, d = inner[0] - X0, inner[1] - Y0, inner[2] - X0, inner[3] - Y0   # область содержимого
    L = smooth1d(t[b:d, a - 1], 8); R = smooth1d(t[b:d, c], 8)
    T = linfit(t[b - 1, a:c]); B = linfit(t[d, a:c])   # верх/низ: только плавный наклон, без следов иконки
    h, w = d - b, c - a
    u = (np.arange(w) + 0.5) / w; v = (np.arange(h) + 0.5) / h
    U, V = u[None, :, None], v[:, None, None]
    P00, P10, P01, P11 = t[b - 1, a - 1], t[b - 1, c], t[d, a - 1], t[d, c]
    lr = (1 - U) * L[:, None] + U * R[:, None]
    tb = (1 - V) * T[None] + V * B[None]
    bil = (1 - U) * (1 - V) * P00 + U * (1 - V) * P10 + (1 - U) * V * P01 + U * V * P11
    t[b:d, a:c] = lr + tb - bil
    rgb = Image.fromarray(np.clip(t, 0, 255).astype(np.uint8))
    S = 8                                       # маска со сглаживанием
    m = Image.new("L", ((X1 - X0) * S, (Y1 - Y0) * S), 0)
    ImageDraw.Draw(m).rounded_rectangle(((x0 - X0) * S, (y0 - Y0) * S, (x1 - X0) * S - 1, (y1 - Y0) * S - 1),
                                        radius * S, fill=255)
    rgb.putalpha(m.resize(rgb.size, Image.LANCZOS))
    rgb.save(out)
    print(out, rgb.size)

R = sys.argv[1] + "/"; O = sys.argv[2]
make(R + "t1000_activities.png", (100, 167.5, 329.5, 394), (118, 188, 313, 381), 13, O + "/tile_1000.png")
make(R + "t1100_activities.png", (109.5, 109.5, 339.5, 337.5), (119, 118, 330, 328), 13, O + "/tile_1100.png")
