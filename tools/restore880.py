"""Восстановление zones.json для 880: именованные зоны в формате редактора."""
import json
from pathlib import Path

# Точные рамки из ручного файла (зафиксированы в отладочных прогонах) +
# остальное — из автодетекции/зеркальных позиций. Формат как у других моделей.
zones = [
    # верх
    {"id": "Off", "rect": [207.5, 40, 34, 90], "radius": 10.2, "kind": "System"},
    {"id": "MoreActivities", "rect": [96, 110, 111.5, 35.5], "radius": 10.65, "kind": "Key"},
    {"id": "Help", "rect": [239, 110, 120, 35.5], "radius": 10.65, "kind": "Key"},
    # софт-кнопки у экрана
    {"id": "Left1", "rect": [54, 240, 26, 63], "radius": 7.8, "kind": "Key"},
    {"id": "Left2", "rect": [50, 303, 24, 62], "radius": 7.2, "kind": "Key"},
    {"id": "Lef3", "rect": [50, 365, 24, 66], "radius": 7.2, "kind": "Key"},
    {"id": "Lef4", "rect": [50, 431, 24, 61], "radius": 7.2, "kind": "Key"},
    {"id": "Right1", "rect": [372, 240, 26, 60], "radius": 7.8, "kind": "Key"},
    {"id": "Right2", "rect": [372, 303, 26, 62], "radius": 7.8, "kind": "Key"},
    {"id": "Right3", "rect": [372, 365, 26, 66], "radius": 7.8, "kind": "Key"},
    {"id": "Right4", "rect": [372, 431, 26, 61], "radius": 7.8, "kind": "Key"},
    # ниже экрана
    {"id": "GLOW", "rect": [251, 495, 104, 34], "radius": 10.2, "kind": "Key"},
    {"id": "Devices", "rect": [96, 495, 107, 34], "radius": 10.2, "kind": "Key"},
    {"id": "PageLeft", "rect": [172, 549, 104, 39], "radius": 11.7, "kind": "Key"},
    {"id": "PageRight", "rect": [262, 549, 78, 39], "radius": 11.7, "kind": "Key"},
    {"id": "UpArrow", "rect": [199, 540, 53, 26], "radius": 7.8, "kind": "Key"},
    {"id": "DownArrow", "rect": [188, 602, 76, 70], "radius": 21.0, "kind": "Key"},
    {"id": "Exit", "rect": [101, 608, 69, 47], "radius": 14.1, "kind": "Key"},
    {"id": "Guide", "rect": [263, 636, 69, 83], "radius": 20.7, "kind": "Key"},
    # цветные кнопки
    {"id": "Red", "rect": [62, 660, 44, 40], "radius": -0.5, "kind": "Key"},
    {"id": "Green", "rect": [160, 660, 44, 40], "radius": -0.5, "kind": "Key"},
    {"id": "Yellow", "rect": [258, 660, 44, 40], "radius": -0.5, "kind": "Key"},
    {"id": "Blue", "rect": [356, 660, 44, 40], "radius": -0.5, "kind": "Key"},
    # VOL / CH / D-pad
    {"id": "VolumeUp", "rect": [95, 750, 44, 48], "radius": 13.2, "kind": "Key"},
    {"id": "VolumeDown", "rect": [95, 802, 44, 48], "radius": 13.2, "kind": "Key"},
    {"id": "ChannelUp", "rect": [347, 750, 44, 48], "radius": 13.2, "kind": "Key"},
    {"id": "ChannelDown", "rect": [347, 802, 44, 48], "radius": 13.2, "kind": "Key"},
    {"id": "DirectionUp", "rect": [172, 750, 48, 44], "radius": 13.2, "kind": "Key"},
    {"id": "DirectionDown", "rect": [252, 750, 48, 44], "radius": 13.2, "kind": "Key"},
    {"id": "Select", "rect": [209, 812, 48, 48], "radius": -0.5, "kind": "Key"},
    {"id": "Mute", "rect": [326, 966, 52, 64], "radius": 15.6, "kind": "Key"},
    # транспорт
    {"id": "Rewind", "rect": [137, 945, 49, 81], "radius": 14.7, "kind": "Key"},
    {"id": "Pause", "rect": [249, 945, 65, 82], "radius": 19.5, "kind": "Key"},
    {"id": "Play", "rect": [312, 1043, 88, 58], "radius": 17.4, "kind": "Key"},
    {"id": "Record", "rect": [52, 1045, 78, 56], "radius": 16.8, "kind": "Key"},
    {"id": "Stop", "rect": [293, 1039, 90, 80], "radius": 24.0, "kind": "Key"},
    {"id": "FastForward", "rect": [351, 1117, 52, 64], "radius": 15.6, "kind": "Key"},
    {"id": "SkipBack", "rect": [78, 1051, 44, 56], "radius": 13.2, "kind": "Key"},
    {"id": "SkipForward", "rect": [364, 1118, 52, 64], "radius": 15.6, "kind": "Key"},
    # цифровая панель
    {"id": "Number1", "rect": [57, 1163, 56, 54], "radius": -0.5, "kind": "Key"},
    {"id": "Number2", "rect": [178, 1163, 62, 54], "radius": -0.5, "kind": "Key"},
    {"id": "Number3", "rect": [298, 1163, 62, 54], "radius": -0.5, "kind": "Key"},
    {"id": "Number4", "rect": [57, 1223, 56, 54], "radius": -0.5, "kind": "Key"},
    {"id": "Number5", "rect": [178, 1223, 62, 54], "radius": -0.5, "kind": "Key"},
    {"id": "Number6", "rect": [298, 1223, 62, 54], "radius": -0.5, "kind": "Key"},
    {"id": "Number7", "rect": [57, 1281, 56, 54], "radius": -0.5, "kind": "Key"},
    {"id": "Number8", "rect": [178, 1281, 62, 54], "radius": -0.5, "kind": "Key"},
    {"id": "Number9", "rect": [298, 1281, 62, 54], "radius": -0.5, "kind": "Key"},
    {"id": "NumberPlus", "rect": [57, 1337, 56, 54], "radius": -0.5, "kind": "Key"},
    {"id": "Number0", "rect": [178, 1337, 62, 54], "radius": -0.5, "kind": "Key"},
    {"id": "NumberEnter", "rect": [298, 1337, 62, 54], "radius": -0.5, "kind": "Key"},
    {"id": "NumberClear", "rect": [60, 1418, 50, 46], "radius": -0.5, "kind": "Key"},
    {"id": "NumberEnterWide", "rect": [140, 1436, 64, 52], "radius": -0.5, "kind": "Key"},
]

out = Path("dist/win/resources/remotes/880/zones.json")
out.write_text(json.dumps(zones, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("зон:", len(zones), "->", out)
