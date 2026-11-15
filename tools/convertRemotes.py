#!/usr/bin/env python3
"""Собирает единый remotes/<id>/remote.json из profile.json + zones.json + screens.json + layouts.json.

Запуск: python tools/convertRemotes.py [--delete-old]
Без --delete-old старые файлы остаются (удобно сравнить и собрать приложение).
Формат remote.json описан в app/resources/remotes/SCHEMA.md.
"""
import json
import sys
from pathlib import Path

RES = Path(__file__).resolve().parents[1] / "app" / "resources"
DEFAULT_BG = "#0b3442"      # цвет фона экрана по умолчанию (как у 900)
DEFAULT_CELL = "#0b3442"    # заливка ячейки по умолчанию
DEFAULT_BORDER = "#ffffff"  # белая рамка

ROLE = {"activities": "activity", "favorites": "favorites", "devices": "devices",
        "command": "command", "transport": "media", "keypad": "numpad"}
# Спрайты 900, если у модели нет своих (картинки есть -> используем, нет -> заливка цветом)
SPRITES_900 = {"background": "sprites/background_900.png", "tile": "sprites/tile_900.png"}


def load(path, default=None):
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else default


def convert(model_dir: Path, layouts: dict) -> dict:
    profile = load(model_dir / "profile.json")
    zones = load(model_dir / "zones.json", [])
    sprites = load(model_dir / "screens.json", {})
    out = {
        "schemaVersion": 1,
        "id": profile["id"],
        "name": profile["name"],
        "capabilities": {"touch": profile.get("touch", False)},
        "connection": {"driver": {"harmony900": "usb-net", "hid": "usb-hid"}.get(profile.get("driver"), "none"),
                       "format": {"harmony900": "harmony-xml"}.get(profile.get("configFormat"), "none")},
        "hardware": {"image": profile["image"], "size": profile["size"], "zones": zones},
        "display": {"rect": profile.get("screen"), "tint": profile.get("screenTint")},
        "screens": [],
    }
    if out["display"]["tint"] is None:
        del out["display"]["tint"]
    images = {k: v["file"] for k, v in sprites.get("images", {}).items()}
    for mode, layout_id in profile.get("modes", {}).items():
        layout = layouts[layout_id]
        screen = {
            "id": mode,
            "role": ROLE.get(mode, mode),
            "layout": layout_id,                       # исходная раскладка (для отладки миграции)
            "ref": layout["ref"],
            "background": {"color": DEFAULT_BG},
            "cell": {"color": DEFAULT_CELL, "border": DEFAULT_BORDER},
            "cells": layout["zones"],                  # у каждой ячейки явный rect в единицах ref
        }
        for key in ("panel", "centerLastRow", "compose"):
            if key in layout:
                screen[key] = layout[key]
        if "tile" in layout:
            screen["cell"].update(layout["tile"])      # image/shadow/text/color/border
        if mode in sprites.get("screens", {}):
            layers = []
            for item in sprites["screens"][mode]:
                item = dict(item)
                item["image"] = images[item["image"]]  # путь вместо ключа
                layers.append(item)
            screen["layers"] = layers
            screen["background"]["image"] = next(
                (l["image"] for l in layers if "background" in l["image"]), None)
            if screen["background"]["image"] is None:
                del screen["background"]["image"]
        out["screens"].append(screen)
    return out


def main():
    layouts = {l["id"]: l for l in load(RES / "layouts.json")}
    delete_old = "--delete-old" in sys.argv
    for model_dir in sorted((RES / "remotes").iterdir()):
        if not (model_dir / "profile.json").exists():
            continue
        data = convert(model_dir, layouts)
        (model_dir / "remote.json").write_text(
            json.dumps(data, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        print("wrote", model_dir.name, "screens:", [s["id"] for s in data["screens"]])
        if delete_old:
            for name in ("profile.json", "zones.json", "screens.json"):
                (model_dir / name).unlink(missing_ok=True)
    if delete_old:
        (RES / "layouts.json").unlink(missing_ok=True)


if __name__ == "__main__":
    main()
