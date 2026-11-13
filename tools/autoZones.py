"""Авто-распознавание зон кнопок на фото пульта (photo.png с прозрачным фоном).

Режимы:
  python tools/autoZones.py <папка модели|фото.png> --json
      Только JSON-массив рамок [[x, y, w, h], …] в stdout — для кнопки «Авто-зоны» приложения.
  python tools/autoZones.py <папка модели> <папка вывода>
      Сравнение с ручным zones.json и отладочный оверлей overlay.png.

Рамки не пересекаются друг с другом, не заходят на прозрачный фон и на экран (profile.json: screen).
"""
import json
import sys
from pathlib import Path

import cv2
import numpy as np

# Доля непрозрачных пикселей в рамке: меньше — рамка задевает фон вокруг пульта.
OPAQUE = 0.97
# Пороги Canny для поиска «гладких» областей кнопок: фото разные, один порог не подходит всем.
CANNY_PASSES = ((30, 90), (60, 180), (15, 45), (8, 25), (100, 250))


def iou(a, b):
    inter = overlap(a, b)
    union = a[2] * a[3] + b[2] * b[3] - inter
    return inter / union if union else 0.0


def overlap(a, b):
    """Площадь пересечения рамок (x, y, w, h)."""
    x0 = max(a[0], b[0]); y0 = max(a[1], b[1])
    x1 = min(a[0] + a[2], b[0] + b[2]); y1 = min(a[1] + a[3], b[1] + b[3])
    return max(0, x1 - x0) * max(0, y1 - y0)


def load(photo: Path):
    bgr = cv2.imread(str(photo), cv2.IMREAD_UNCHANGED)
    if bgr is None or bgr.ndim != 3 or bgr.shape[2] != 4:
        raise SystemExit(f"нужно PNG с прозрачным фоном: {photo}")
    screen = None
    profile = photo.parent / "profile.json"
    if profile.is_file():
        prof = json.loads(profile.read_text(encoding="utf-8"))
        if prof.get("screen"):
            screen = tuple(int(v) for v in prof["screen"])
    return bgr, screen


def smooth_regions(gray, body):
    """Кнопка — гладкая выпуклая область, окружённая контуром.

    Для каждого порога Canny ищем связные области без границ внутри корпуса и оставляем
    компактные выпуклые. Размеры — в долях корпуса (sqrt площади), чтобы не зависеть от масштаба фото.
    """
    size = float(np.sqrt(body.sum()))
    blur = cv2.GaussianBlur(gray, (3, 3), 0)
    inner = cv2.erode(body, np.ones((3, 3), np.uint8), iterations=max(1, int(0.02 * size)))
    found = []
    for lo, hi in CANNY_PASSES:
        edges = cv2.dilate(cv2.Canny(blur, lo, hi), np.ones((2, 2), np.uint8))
        free = ((edges == 0) & (inner > 0)).astype(np.uint8) * 255
        contours, _ = cv2.findContours(free, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
        for c in contours:
            area = cv2.contourArea(c)
            if not (0.0025 * size * size <= area <= 0.06 * size * size):
                continue
            x, y, w, h = cv2.boundingRect(c)
            hull = cv2.contourArea(cv2.convexHull(c))
            if area / (w * h) < 0.6 or hull <= 0 or area / hull < 0.75:
                continue
            if not (0.2 <= h / w <= 5):
                continue
            found.append((x - 3, y - 3, w + 6, h + 6))
    return found


def shaped_buttons(rgb, gray, body):
    """Кнопки, которые плохо видны как гладкие области: глянцевые контуры, матовые клавиши,
    светлые надписи на тёмном корпусе, LED-точки софт-кнопок."""
    hsv = cv2.cvtColor(rgb, cv2.COLOR_BGR2HSV)
    colorful = ((hsv[:, :, 1] > 60) & body).astype(np.uint8) * 255
    colorful = cv2.dilate(colorful, np.ones((5, 5), np.uint8), iterations=1) > 0
    blur = cv2.GaussianBlur(gray, (3, 3), 0)
    H, W = gray.shape
    raw = []
    # замкнутые контуры — выпуклые глянцевые кнопки
    edges = cv2.Canny(blur, 40, 120)
    contours, _ = cv2.findContours(edges, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_SIMPLE)
    for c in contours:
        area = cv2.contourArea(c)
        if area < 400:
            continue
        x, y, w, h = cv2.boundingRect(c)
        if not (12 <= w <= 110 and 12 <= h <= 130) or not (0.35 <= h / w <= 4.5):
            continue
        perim = cv2.arcLength(c, True)
        if perim <= 0 or 4 * np.pi * area / (perim * perim) < 0.4:
            continue
        raw.append((x, y, w, h))
    # компоненты адаптивного порога — матовые клавиши
    m = cv2.adaptiveThreshold(blur, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C, cv2.THRESH_BINARY, 101, 15)
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, np.ones((7, 7), np.uint8))
    m[~body] = 0
    n, _, stats, _ = cv2.connectedComponentsWithStats(m, 8)
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if 600 <= area <= 9000 and 20 <= w <= 150 and 14 <= h <= 140 and 0.25 <= h / w <= 4.0:
            raw.append((x - 4, y - 4, w + 8, h + 8))
    # светлые надписи на тёмном корпусе (ACTIVITIES, HELP…), сгруппированные в кнопку
    t = cv2.adaptiveThreshold(blur, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C, cv2.THRESH_BINARY, 101, -15)
    t[~body] = 0
    n, _, stats, _ = cv2.connectedComponentsWithStats(t, 8)
    spots = np.zeros_like(t)
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if 30 <= area <= 1500 and 4 <= w <= 110 and 3 <= h <= 80 and colorful[y:y + h, x:x + w].mean() <= 0.5:
            spots[y:y + h, x:x + w] = 255
    n, _, stats, _ = cv2.connectedComponentsWithStats(cv2.dilate(spots, np.ones((15, 15), np.uint8)), 8)
    for i in range(1, n):
        gx, gy, gw, gh, _ = stats[i]
        ys, xs = np.where(spots[gy:gy + gh, gx:gx + gw] > 0)
        if not len(xs):
            continue
        x0, y0 = gx + int(xs.min()) - 8, gy + int(ys.min()) - 8
        x1, y1 = gx + int(xs.max()) + 8, gy + int(ys.max()) + 8
        if 14 <= x1 - x0 <= 170 and 10 <= y1 - y0 <= 150:
            raw.append((x0, y0, x1 - x0, y1 - y0))
    # тёмные LED-точки на светлом фоне (софт-кнопки у экрана)
    local = cv2.GaussianBlur(gray, (51, 51), 0)
    dark = (gray < local - 25) & body
    n, _, stats, _ = cv2.connectedComponentsWithStats(dark.astype(np.uint8) * 255, 8)
    for i in range(1, n):
        x, y, w, h, area = stats[i]
        if 10 <= area <= 90 and 3 <= w <= 16 and 3 <= h <= 16 and colorful[y:y + h, x:x + w].mean() <= 0.3:
            raw.append((int(x + w / 2 - 22), int(y + h / 2 - 28), 44, 56))
    # цветной экран (LCD) не размечаем
    return [b for b in raw if colorful[max(0, b[1]):b[1] + b[3], max(0, b[0]):b[0] + b[2]].mean() <= 0.5]


def detect(photo: Path):
    bgr, screen = load(photo)
    rgb = bgr[:, :, :3]  # на самом деле BGR (загрузка cv2)
    alpha = bgr[:, :, 3] > 128
    body = alpha.astype(np.uint8)
    gray = cv2.cvtColor(rgb, cv2.COLOR_BGR2GRAY)
    H, W = gray.shape

    # Гладкие области надёжнее — вес 2, остальные детекторы — 1.
    candidates = [[*b, 2.0] for b in smooth_regions(gray, body)]
    candidates += [[*b, 1.0] for b in shaped_buttons(rgb, gray, alpha)]
    good = []
    for x, y, w, h, weight in candidates:
        x, y, w, h = int(x), int(y), int(w), int(h)
        if x < 0 or y < 0 or x + w > W or y + h > H or w < 10 or h < 8:
            continue
        if alpha[y:y + h, x:x + w].mean() < OPAQUE:
            continue  # задевает прозрачный фон — не кнопка
        if screen and overlap((x, y, w, h), screen) > 0.2 * w * h:
            continue
        good.append([x, y, w, h, weight])
    # Рамку, которую нашли несколько проходов, считаем надёжнее.
    for g in good:
        g[4] += 0.5 * sum(iou(g, o) > 0.6 for o in good)
    good.sort(key=lambda g: -g[4])
    # Жадный отбор без пересечений: лучшая рамка занимает место, остальные на нём не ставятся.
    kept = []
    for g in good:
        if all(overlap(g, k) == 0 for k in kept):
            kept.append(g)
    kept.sort(key=lambda g: (g[1], g[0]))
    return rgb, [tuple(k[:4]) for k in kept]


def main():
    args = sys.argv[1:]
    json_mode = "--json" in args
    args = [a for a in args if a != "--json"]
    if len(args) < 1 or (not json_mode and len(args) < 2):
        raise SystemExit(__doc__)
    path = Path(args[0])
    photo = path if path.is_file() else path / "photo.png"
    if not photo.is_file():
        raise SystemExit(f"фото не найдено: {photo}")
    rgb, boxes = detect(photo)
    if json_mode:
        # В stdout — только JSON-массив рамок; прочее приложение не должно читать.
        print(json.dumps([[int(x), int(y), int(w), int(h)] for x, y, w, h in boxes]))
        return
    out = Path(args[1])
    out.mkdir(parents=True, exist_ok=True)
    manual = json.loads((photo.parent / "zones.json").read_text(encoding="utf-8"))
    print(f"найдено зон: {len(boxes)}, ручных зон: {len(manual)}")
    ov = rgb.copy()
    n_match = 0
    for i, (x, y, w, h) in enumerate(boxes):
        cv2.rectangle(ov, (x, y), (x + w, y + h), (0, 255, 0), 2)
        cv2.putText(ov, str(i), (x, y - 3), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 0), 1)
        best, mid = max(((iou((x, y, w, h), z["rect"]), z["id"]) for z in manual), default=(0.0, ""))
        n_match += best >= 0.5
        print(f"  [{i:2d}] {x:3d},{y:3d} {w:3d}x{h:3d}  IoU={best:.2f} {'~' + mid if best >= 0.5 else '-'}")
    for z in manual:
        x, y, w, h = (int(round(v)) for v in z["rect"])  # в zones.json бывают дроби
        cv2.rectangle(ov, (x, y), (x + w, y + h), (255, 80, 0), 2)
    cv2.imwrite(str(out / "overlay.png"), ov)
    print(f"совпало (IoU>=0.5): {n_match}/{len(manual)}")


if __name__ == "__main__":
    main()
